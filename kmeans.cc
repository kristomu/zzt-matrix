// Use k-means++ quantization to turn a picture into something that can
// be rendered in text mode (with custom charset and palette, and a fixed
// caption).

#include <unistd.h>
#include <stdlib.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <map>
#include <set>

#include <math.h>
#include <float.h>

#include "pixel.h"

#include "delta_e_2000.cc"

// VGA palette sets 0 = completely dark, 63 = full brightness
const int COL_SHADES = 63.0;

struct dos_char_col {
	int character; // <--- !!! admits values > 255!
	int fg_color;
	int bg_color;
};

class error_with_coord {
	public:
		int x, y;
		double error;

		error_with_coord() {}
		error_with_coord(int x_in, int y_in, double err_in) {
			x = x_in;
			y = y_in;
			error = err_in;
		};

		bool operator<(const error_with_coord & other) const {
			if (error == other.error) {
				if (y == other.y) {
					return x < other.x;
				}
				return y < other.y;
			}
			return error < other.error;
		}
};

class quant_error {
	public:
		double total_error;
		std::set<error_with_coord> quant_errors;

		quant_error() {
			total_error = 0;
			quant_errors.clear();
		}
};

typedef std::vector<std::vector<pixel> > Image;
typedef std::vector<std::vector<bool> > DOSChar;
typedef std::vector<DOSChar> Charset;
typedef std::vector<pixel> Palette;

typedef std::vector<std::vector<dos_char_col> > TImage;

/*#define PALETTE_SIZE 16
#define CHARSET_SIZE 256
#define CHAR_HEIGHT 8
#define CHAR_WIDTH 8*/

std::string itos(int number) {
	std::ostringstream xav;
	xav << number;
	return(xav.str());
}


// ARRR! There be no error handling here, matey!
Image read_ppm(std::string fn) {
	Image image;

	std::ifstream in_ppm(fn);
	std::vector<std::string> metadata;
	int lines_read = 0;

	while (lines_read < 3) {
		std::string line;
		std::getline(in_ppm, line);
		// Skip comments.
		if(line[0] != '#') {
			metadata.push_back(line);
			++lines_read;
		}
	}

	if (metadata[0] != "P6" || metadata[2] != "255") {
		throw std::runtime_error("Unsupported PPM type for file " + fn);
	}

	std::stringstream num_extractor;
	num_extractor << metadata[1];
	size_t width, height;
	num_extractor >> width;
	num_extractor >> height;

	std::cout << "Reading file " << fn << ", " << width << "x" << height << "\n";
	for (int y = 0; y < height; ++y) {
		image.push_back(std::vector<pixel>());
		for (int x = 0; x < width; ++x) {
			pixel out;
			// The next is very slow but whatever.
			in_ppm.read((char *)&out.r, 1);
			in_ppm.read((char *)&out.g, 1);
			in_ppm.read((char *)&out.b, 1);
			image[y].push_back(out);
		}
	}
	return image;
}

void write_ppm(const Image & image,
	std::string fn) {

	std::ofstream out_ppm(fn);

	size_t height = image.size(), width = image[0].size();

	out_ppm << "P6\n" << width << " " << height << "\n";
	out_ppm << "255\n";

	for (size_t y = 0; y < height; ++y) {
		for (size_t x = 0; x < width; ++x) {
			out_ppm << image[y][x].r << image[y][x].g << image[y][x].b;
		}
	}
	out_ppm.close();
}

// http://www.easyrgb.com/en/math.php
template<typename T> T rgb_to_xyz(const T & in) {
	double var_R = in.r / 255.0;
	double var_G = in.g / 255.0;
	double var_B = in.b / 255.0;

	if ( var_R > 0.04045 ) var_R = pow(( ( var_R + 0.055 ) / 1.055 ), 2.4);
	else                   var_R = var_R / 12.92;
	if ( var_G > 0.04045 ) var_G = pow(( ( var_G + 0.055 ) / 1.055 ), 2.4);
	else                   var_G = var_G / 12.92;
	if ( var_B > 0.04045 ) var_B = pow(( ( var_B + 0.055 ) / 1.055 ), 2.4);
	else                   var_B = var_B / 12.92;

	var_R = var_R * 100;
	var_G = var_G * 100;
	var_B = var_B * 100;

	T out;

	double X = var_R * 0.4124 + var_G * 0.3576 + var_B * 0.1805;
	double Y = var_R * 0.2126 + var_G * 0.7152 + var_B * 0.0722;
	double Z = var_R * 0.0193 + var_G * 0.1192 + var_B * 0.9505;

	out.r = X;
	out.g = Y;
	out.b = Z;

	return out;
}

template<typename T> T xyz_to_lab(const T & in) {
	// 2 degrees D65 standard illuminant
	double var_X = in.r / 95.047;
	double var_Y = in.g / 100.000;
	double var_Z = in.b / 108.883;

	if ( var_X > 0.008856 ) var_X = pow(var_X, 1/3.0 );
	else                    var_X = ( 7.787 * var_X ) + ( 16 / 116.0 );
	if ( var_Y > 0.008856 ) var_Y = pow(var_Y, 1/3.0);
	else                    var_Y = ( 7.787 * var_Y ) + ( 16 / 116.0 );
	if ( var_Z > 0.008856 ) var_Z = pow(var_Z, 1/3.0);
	else                    var_Z = ( 7.787 * var_Z ) + ( 16 / 116.0 );

	T out;

	out.r = ( 116 * var_Y ) - 16;		// L
	out.g = 500 * ( var_X - var_Y );	// a
	out.b = 200 * ( var_Y - var_Z );	// b

	return out;
}

template<typename T> T lab_to_xyz(const T & in) {
	double var_Y = ( in.r + 16 ) / 116;
	double var_X = in.g / 500 + var_Y;
	double var_Z = var_Y - in.b / 200;

	if ( pow(var_Y, 3)  > 0.008856 ) var_Y = pow(var_Y, 3);
	else                       var_Y = ( var_Y - 16 / 116.0 ) / 7.787;
	if ( pow(var_X, 3)  > 0.008856 ) var_X = pow(var_X, 3);
	else                       var_X = ( var_X - 16 / 116.0 ) / 7.787;
	if ( pow(var_Z, 3)  > 0.008856 ) var_Z = pow(var_Z, 3);
	else                       var_Z = ( var_Z - 16 / 116.0 ) / 7.787;

	T out;

	out.r = var_X * 95.047;
	out.g = var_Y * 100;
	out.b = var_Z * 108.883;

	return out;
}

template<typename T> T xyz_to_rgb(const T & in) {

	double var_X = in.r / 100;
	double var_Y = in.g / 100;
	double var_Z = in.b / 100;

	double var_R = var_X *  3.2406 + var_Y * -1.5372 + var_Z * -0.4986;
	double var_G = var_X * -0.9689 + var_Y *  1.8758 + var_Z *  0.0415;
	double var_B = var_X *  0.0557 + var_Y * -0.2040 + var_Z *  1.0570;

	if ( var_R > 0.0031308 ) var_R = 1.055 * pow( var_R, ( 1 / 2.4 ) ) - 0.055;
	else                     var_R = 12.92 * var_R;
	if ( var_G > 0.0031308 ) var_G = 1.055 * pow( var_G, ( 1 / 2.4 ) ) - 0.055;
	else                     var_G = 12.92 * var_G;
	if ( var_B > 0.0031308 ) var_B = 1.055 * pow( var_B, ( 1 / 2.4 ) ) - 0.055;
	else                     var_B = 12.92 * var_B;

	T out;

	out.r = var_R * 255.0;
	out.g = var_G * 255.0;
	out.b = var_B * 255.0;

	return out;
}

dblpixel rgb_to_lab(const pixel & a) {
	dblpixel ap(a);
	return xyz_to_lab(rgb_to_xyz(a));
}

dblpixel lab_to_rgb(const dblpixel & a) {
	return xyz_to_rgb(lab_to_xyz(a));
}

double sqr(double x) { return x * x; }
double sqrd(double x, double y) { return sqr(x-y); }

double cie94_sqd(const dblpixel & lab_a, const dblpixel & lab_b) {

	double deltaL = lab_a.r - lab_b.r;
	double deltaA = lab_a.g - lab_b.g;
	double deltaB = lab_a.b - lab_b.b;

	double c1 = sqrt(sqr(lab_a.g) + sqr(lab_a.b));
	double c2 = sqrt(sqr(lab_b.g) + sqr(lab_b.b));
	double deltaC = c1 - c2;

	double deltaH = sqr(deltaA) + sqr(deltaB) - sqr(deltaC);
	deltaH = deltaH < 0 ? 0 : sqrt(deltaH);

	const double sl = 1.0;
	const double kc = 1.0;
	const double kh = 1.0;

	double Kl = 1.0;
    double K1 = 0.045;
    double K2 = 0.015;

	double sc = 1.0 + K1*c1;
	double sh = 1.0 + K2*c1;

	double i = sqr(deltaL/(Kl*sl)) +
		sqr(deltaC/(kc*sc)) + sqr(deltaH/(kh*sh));

	return i;
}

/*double sqrd(const pixel & a, const pixel & b) {
	return sqrd(a.r, b.r) + sqrd(a.g, b.g) + sqrd(a.b, b.b);
}*/
double lab_sqrd(const dblpixel & a, const dblpixel & b) {
	//return sqrd(c.r, d.r) + sqrd(c.g, d.g) + sqrd(c.b, d.b);
	return cie94_sqd(a, b);
	//return de00(a, b, true);
}
double sqrd(const pixel & a, const pixel & b) {
	dblpixel c(a), d(b);
	c = xyz_to_lab(rgb_to_xyz(c));
	d = xyz_to_lab(rgb_to_xyz(d));
	return lab_sqrd(c, d);
}

double get_error(const Image & a, const Image & b) {

	double squared_error = 0;
	for (size_t y = 0; y < a.size(); ++y) {
		for (size_t x = 0; x < a[0].size(); ++x) {
			squared_error += sqrd(a[y][x], b[y][x]);
		}
	}

	return sqrt(squared_error / double(a.size() * a[0].size()));
}

pixel random_color() {
	pixel out;
	out.r = random() % 256;
	out.g = random() % 256;
	out.b = random() % 256;

	return out;
}

Charset random_charset(int num_chars, int char_width, int char_height) {
	Charset out;

	for (size_t i = 0; i < num_chars; ++i) {
		DOSChar this_char;
		int density = random() % 4 +1;
		for (size_t y = 0; y < char_height; ++y) {
			this_char.push_back(std::vector<bool>());
			for (size_t x = 0; x < char_width; ++x) {
				this_char[y].push_back(random() % density == 0);
			}
		}
		out.push_back(this_char);
	}

	return out;
}

Charset blank_charset(int num_chars, int char_width, int char_height) {
	Charset out;

	for (size_t i = 0; i < num_chars; ++i) {
		DOSChar this_char;
		int density = random() % 4 +1;
		for (size_t y = 0; y < char_height; ++y) {
			this_char.push_back(std::vector<bool>(char_width, true));
		}
		out.push_back(this_char);
	}

	return out;
}

Palette random_palette(int num_colors) {
	Palette out;

	for (size_t i = 0; i < num_colors; ++i) {
		pixel x;
		x.r = random();
		x.g = random();
		x.b = random();
		out.push_back(x);
	}

	return out;
}

void render_char(const DOSChar character, const pixel fg_color,
	const pixel bg_color, Image & dest, int x_offset, int y_offset) {

	for (size_t y = 0; y < character.size(); ++y) {
		for (size_t x = 0; x < character[0].size(); ++x) {
			if (character[y][x]) {
				dest[y+y_offset][x+x_offset] = fg_color;
			} else {
				dest[y+y_offset][x+x_offset] = bg_color;
			}
		}
	}
}

// Get the contribution to the squared error by either the foreground
// or the background color.
double get_sq_error(const DOSChar & character,
	const std::vector<std::vector<double> > & color_errors,
	bool is_fg) {

	double error = 0;

	for (int y = 0; y < character.size(); ++y) {
		for (int x = 0; x < character[0].size(); ++x) {
			if (character[y][x] == is_fg) {
				error += color_errors[y][x];
			}
		}
	}

	return error;
}

dos_char_col get_best_quantization(const Image & input,
	const Charset & charset, const Palette & palette, int input_x,
	int input_y, Image & char_render, double & error_out) {

	size_t char_height = charset[0].size(), char_width = charset[0][0].size();
	size_t color;

	double record_error = DBL_MAX;

	dos_char_col champion, current;

	// Memoize the squared distance for each palette color, for each
	// pixel.
	std::vector<std::vector<std::vector<double> > > all_errors(palette.size(),
		std::vector<std::vector<double> >(char_height,
		std::vector<double> (char_width)));

	for (color = 0; color < palette.size(); ++color) {
		for (int y = 0; y < char_height; ++y) {
			for (int x = 0; x < char_width; ++x) {
				all_errors[color][y][x] = sqrd(
					input[input_y+y][input_x+x],
					palette[color]);
			}
		}
	}

	for (current.character = 0; current.character < charset.size();
		++current.character) {

		// With the character given, find out the best foreground color...
		double min_fg = DBL_MAX;

		for (color = 0; color < palette.size(); ++color) {
			double cand_fg_error = get_sq_error(charset[current.character],
				all_errors[color], true);
			if (min_fg > cand_fg_error) {
				current.fg_color = color;
				min_fg = cand_fg_error;
			}
		}

		if (min_fg > record_error) { continue; }

		// ... and the best background color.
		double min_bg = DBL_MAX;

		for (color = 0; color < palette.size(); ++color) {
			double cand_bg_error = get_sq_error(charset[current.character],
				all_errors[color], false);
			if (min_bg > cand_bg_error) {
				current.bg_color = color;
				min_bg = cand_bg_error;
			}
		}

		if (min_fg + min_bg < record_error) {
			champion = current;
			record_error = min_fg + min_bg;
		}
	}
	error_out = record_error;
	return champion;
}

TImage quantize_image(const Image & input, const Charset & charset,
	const Palette & palette, quant_error & qerrors_out) {

	size_t char_height = charset[0].size(), char_width = charset[0][0].size();
	size_t img_height = input.size(), img_width = input[0].size();

	if (img_height % char_height != 0) {
		throw std::runtime_error("Height not divisible by char height!");
	}

	if (img_width % char_width != 0) {
		throw std::runtime_error("Width not divisible by char width!");
	}

	std::cout << char_width << ", " << char_height << std::endl;
	std::cout << img_width << ", " << img_height << std::endl;

	Image char_render(std::vector<std::vector<pixel> >(char_height,
		std::vector<pixel>(char_width)));

	qerrors_out.total_error = 0;
	qerrors_out.quant_errors.clear();

	TImage out;
	for (size_t y = 0; y < img_height/char_height; ++y) {
		std::cout << y << "\r  " << std::flush;
		out.push_back(std::vector<dos_char_col>());
		for (size_t x = 0; x < img_width/char_width; ++x) {
			double quantization_error;
			out[y].push_back(get_best_quantization(input, charset, palette,
				x * char_width, y * char_height, char_render,
				quantization_error));
			qerrors_out.quant_errors.insert(error_with_coord(x, y,
				quantization_error));
			qerrors_out.total_error += quantization_error;
		}
	}
	std::cout << std::endl;

	return out;
}

// K-means meets alternating least squares

// Determine the best character that can represent the source image for set
// palette choices. This consists of checking whether the tile's corresponding
// fg color or bg color fits, for each pixel in each tile; if the cumulative
// error of fg is greater than the cumulative error of using bg, the char is
// off at that position, otherwise on.

// Covered_tiles coordinates are in quantized_image coordinates, not image.

DOSChar get_ideal_character(const Image & input,
	const TImage & quantized_image, const Palette & palette,
	const std::vector<std::pair<int, int> > & covered_tiles, int char_width,
	int char_height, double noise) {

	DOSChar out(char_height, std::vector<bool>(char_width));

	for (int y = 0; y < char_height; ++y) {
		for (int x = 0; x < char_width; ++x) {
			double error_by_fg = 0, error_by_bg = 0;
			for (const std::pair<int, int> & tile: covered_tiles) {
				dos_char_col c = quantized_image[tile.first][tile.second];

				error_by_fg += sqrd(palette[c.fg_color],
					input[tile.first * char_height + y]
					[tile.second * char_width + x]);
				error_by_bg += sqrd(palette[c.bg_color],
					input[tile.first * char_height + y]
					[tile.second * char_width + x]);
			}
			out[y][x] = error_by_fg <= error_by_bg;
			if (drand48() <= noise) {
				out[y][x] = !out[y][x];
			}
		}
	}
	return out;
}

// Brute-force all palette combinations to find a good character for the given
// slice of the input image. VERY SLOW. x and y starts are in character
// coordinates.
DOSChar get_brute_ideal_character(const Image & input,
	const Palette & palette, int x_start, int y_start,
	int char_width, int char_height, double noise) {

	DOSChar champion(char_height, std::vector<bool>(char_width)),
		candidate = champion;
	double record_error = DBL_MAX;

	for (int fg = 0; fg < palette.size(); ++fg) {
		for (int bg = 0; bg < palette.size(); ++bg) {
			if (fg == bg) { continue; }

			double cand_error = 0;

			for (int y = 0; y < char_height; ++y) {
				for (int x = 0; x < char_width; ++x) {
					double error_by_fg = 0, error_by_bg = 0;

					error_by_fg += sqrd(palette[fg],
						input[y_start * char_height + y]
							[x_start * char_width + x]);
					error_by_bg += sqrd(palette[bg],
						input[y_start * char_height + y]
							[x_start * char_width + x]);

					candidate[y][x] = error_by_fg <= error_by_bg;
					cand_error += std::min(error_by_bg, error_by_fg);
				}
			}

			if (record_error > cand_error || drand48() <= noise) {
				champion = candidate;
				record_error = cand_error;
			}
		}
	}

	return champion;
}

// Does a roulette selection of a quantized tile based on the error
// of quantizing this tile. Used for kmeans++-like updating of unused
// characters.
std::pair<int, int> roulette_selection(quant_error & from, bool remove_after) {
	double prob = drand48(), seen_so_far = 0;

	if (from.total_error <= 0 || from.quant_errors.size() == 0) {
		throw std::runtime_error(
			"Can't do roulette selection on empty error list!");
	}

	for (std::set<error_with_coord>::reverse_iterator pos =
		from.quant_errors.rbegin(); pos != from.quant_errors.rend(); ++pos) {

		//std::cout << pos->x << ", " << pos->y << " = " << pos->error << std::endl;

		seen_so_far += pos->error/from.total_error;
		if (seen_so_far >= prob) {
			std::pair<int, int>	out(pos->x, pos->y);
			if (remove_after) {
				double error = pos->error;
				from.total_error -= error;
				from.quant_errors.erase(std::next(pos).base());
			}
			return out;
		}
	}
	throw std::logic_error("Error in roulette selection! This shouldn't happen.");
}

// Tile quantization error is used for kmeans++-like updating of unused
// characters.

Charset update_charset(const Image & input, const TImage & quantized_image,
	const Charset & charset_in, const Palette & palette, quant_error &
	tile_quantization_error, double noise) {

	int charset_size = charset_in.size(),
		char_height = charset_in[0].size(),
		char_width = charset_in[0][0].size();

	Charset out;

	size_t i, x, y;

	std::vector<std::vector<std::pair<int, int> > > tiles_covered(
		charset_size);

	// Find covered tiles.
	for (y = 0; y < quantized_image.size(); ++y) {
		for (x = 0; x < quantized_image[0].size(); ++x) {
			tiles_covered[quantized_image[y][x].character].push_back(
				{y, x});
		}
	}

	Charset randset = random_charset(charset_size, char_width, char_height);

	int num_done = 0;
	// Construct better characters.
	for (i = 0; i < charset_size; ++i) {
		// Use a batched version of kmeans++ logic for unused characters, but
		// not too large a batch size. (Another hyperparameter! Stay a while,
		// stay forever!)
		if (tiles_covered[i].size() <= 2 * drand48() * noise) {
			++num_done;
			if (num_done > 6) {
				out.push_back(randset[i]);
				continue;
			}
			std::cout << "Uncovered tile " << i << " detected." << std::endl;
			std::pair<int, int> tile_to_represent = roulette_selection(
				tile_quantization_error, true);
			out.push_back(get_brute_ideal_character(input, palette,
				tile_to_represent.first, tile_to_represent.second,
				char_width, char_height, noise));
			continue;
		}

		out.push_back(get_ideal_character(input, quantized_image,
			palette, tiles_covered[i], char_width, char_height, noise));
	}

	return out;

}

// Poor man's (derivative-less) coordinate descent.
// This is very much a hack.


// Determine the squared error for every pixel rendered to color number
// palette_idx if it were replaced with lab_candidate (in Lab coordinates).
double color_error(const dblpixel & lab_candidate, const Image & input,
	const TImage & text_image, const Image & rendered_image,
	const Palette & palette, size_t palette_idx) {

	size_t y, x;

	size_t char_width = rendered_image[0].size()/text_image[0].size(),
		char_height = rendered_image.size()/text_image.size();

	double error = 0;

	for (y = 0; y < input.size(); ++y) {
		for (x = 0; x < input[0].size(); ++x) {
			if (rendered_image[y][x] == palette[palette_idx] &&
				(text_image[y/char_height][x/char_width].fg_color == palette_idx
					|| text_image[y/char_height][x/char_width].bg_color == palette_idx)) {
				error += lab_sqrd(lab_candidate,
					xyz_to_lab(rgb_to_xyz(dblpixel(input[y][x]))));
			}
		}
	}

	return error;
}

dblpixel find_optimal_color(dblpixel initial_lab_candidate,
	const Image & input, const TImage & text_image,
	const Image & rendered_image, const Palette & palette,
	size_t palette_idx, double noise) {

	int steps = 1;
	double error_t0, starting_error;
	bool has_got_starting_error = false;
	bool improvement = true;

	while (steps > 0) {

		// Find pseudoderivatives (secant lines) in every direction.
		error_t0 = color_error(initial_lab_candidate, input,
			text_image, rendered_image, palette, palette_idx);

		if (!has_got_starting_error) {
			has_got_starting_error = true;
			starting_error = error_t0;
		}

		// Small enough to approximate the derivative, yet large enough
		// to not succumb to numerical instability.
		long double h = 1e-8;
		initial_lab_candidate.r += h;
		long double dr = (color_error(initial_lab_candidate, input, text_image,
			rendered_image, palette, palette_idx) - error_t0) / h;
		initial_lab_candidate.r -= h;
		initial_lab_candidate.g += h;
		long double dg = (color_error(initial_lab_candidate, input, text_image,
			rendered_image, palette, palette_idx) - error_t0) / h;
		initial_lab_candidate.g -= h;
		initial_lab_candidate.b += h;
		long double db = (color_error(initial_lab_candidate, input, text_image,
			rendered_image, palette, palette_idx) - error_t0) / h;

		// Steepest descent.
		long double norm = sqrt(dr*dr + dg*dg + db*db);
		double search_r = -dr/norm, search_g = -dg/norm,
					search_b = -db/norm;

		search_r *= noise * (1 - drand48()) * 0.5;
		search_g *= noise * (1 - drand48()) * 0.5;
		search_b *= noise * (1 - drand48()) * 0.5;

		long double dot = dr*search_r + dg*search_g + db*search_b;

		/*std::cout << error_t0 << std::endl;
		std::cout << dr << ", " << dg << ", " << db << std::endl;*/

		// Move in the direction opposing the secant direction.
		// Poor man's line search. Better is definitely possible.
		double stepsize = 0.1, error_after;
		steps = 0;
		dblpixel out_cand;
		while (improvement) {
			out_cand = initial_lab_candidate;
			out_cand.r = initial_lab_candidate.r + search_r*stepsize;
			out_cand.g = initial_lab_candidate.g + search_g*stepsize;
			out_cand.b = initial_lab_candidate.b + search_b*stepsize;
			error_after = color_error(out_cand, input,
				text_image, rendered_image, palette, palette_idx);
			std::cout << "Giant step: " << error_t0 << " -> " << error_after;
			if (error_after < error_t0) {
				error_t0 = error_after;
				initial_lab_candidate = out_cand;
				stepsize *= sqrt(3);
				++steps;
				std::cout << " improved" << std::endl;
			} else {
				improvement = false;
				std::cout << std::endl;
			}
		}

		/*std::cout << "Backtracking search." << std::endl;

		// Let's try something more proper...
		double t = 1, alpha = 0.5, beta = 0.8;
		double error_reduction;
		do {
			out_cand = initial_lab_candidate;
			out_cand.r = initial_lab_candidate.r + search_r*t;
			out_cand.g = initial_lab_candidate.g + search_g*t;
			out_cand.b = initial_lab_candidate.b + search_b*t;

			error_after = color_error(out_cand, input,
				text_image, rendered_image, palette, palette_idx);

			//error_reduction = starting_error - error_after;
			t = beta * t;
			//std::cout << "progress:" << search_r << "\t" << search_r * t << "\t" << t << std::endl;
			//std::cout << "backtrack: " << error_after << ", " << starting_error + t * dot << std::endl;
		} while (error_after >= error_t0 + alpha * t * dot && t > 0.001);
		std::cout << "Done: " << error_t0 << "\t" << error_after << std::endl;
		improvement = (error_after < error_t0) && norm > 0.1;
		if (improvement) {
			initial_lab_candidate = out_cand;
		}*/
	}

	std::cout << "error: start: " << starting_error << " finish: " << error_t0 << " imp: " << (starting_error-error_t0)/double(starting_error) << std::endl;

	return initial_lab_candidate;
}

// The lazy way, by using a rendered image.
// We're even lazier by using kmeans++ with the already established
// structure.
Palette update_palette(const Image & input, const TImage & text_image,
	const Image & rendered_image, const Palette & palette, double noise) {

	size_t char_width = rendered_image[0].size()/text_image[0].size(),
		char_height = rendered_image.size()/text_image.size();

	Palette out(palette.size());
	std::vector<int> points_with_color(palette.size(), 0);

	quant_error qerror;

	size_t i, x, y;
	bool unused_colors_found = false;

	for (i = 0; i < palette.size(); ++i) {
		dblpixel mean_color;

		for (y = 0; y < input.size(); ++y) {
			for (x = 0; x < input[0].size(); ++x) {
				if (rendered_image[y][x] == palette[i] &&
					(text_image[y/char_height][x/char_width].fg_color == i
						|| text_image[y/char_height][x/char_width].bg_color == i)) {
					++points_with_color[i];
					/*if (drand48() >= noise) {
						mean_color += xyz_to_lab(rgb_to_xyz(dblpixel(input[y][x])));
					} else {
						mean_color += random_color();
					}*/
					double noise_factor = drand48() * noise;
					mean_color += (1 - noise_factor) * xyz_to_lab(rgb_to_xyz(dblpixel(input[y][x]))) +
						noise_factor * dblpixel(random_color());
				}
			}
		}

		// TODO: Make better use of idle colors.
		if (points_with_color[i] == 0) {
			unused_colors_found = true;
			continue;
		}

		mean_color /= points_with_color[i];

		dblpixel best = find_optimal_color(mean_color, input, text_image,
			rendered_image, palette, i, noise);

		dblpixel rgb = xyz_to_rgb(lab_to_xyz(mean_color));

		out[i].r = round(rgb.r);
		out[i].g = round(rgb.g);
		out[i].b = round(rgb.b);
	}

	if (!unused_colors_found) {
		return out;
	}

	for (y = 0; y < input.size(); ++y) {
		for (x = 0; x < input[0].size(); ++x) {
			double pixel_error = sqrd(input[y][x], rendered_image[y][x]);
			qerror.total_error += pixel_error;
			qerror.quant_errors.insert(error_with_coord(x, y, pixel_error));
		}
	}

	for (i = 0; i < palette.size(); ++i) {
		if (points_with_color[i] == 0) {
			std::pair<int, int> color_source = roulette_selection(
				qerror, true);
			out[i] = input[color_source.second][color_source.first];
			std::cout << "Unused color " << i << " detected." << std::endl;
			if (drand48() < 0.5) { return out; }
		}
	}

	return out;
}

Image render_image(const TImage & text_image, const Charset & charset,
	const Palette & palette) {

	size_t char_height = charset[0].size(), char_width = charset[0][0].size();
	size_t img_height = char_height * text_image.size(),
			img_width = char_width * text_image[0].size();

	Image out(img_height, std::vector<pixel>(img_width));

	for (size_t y = 0; y < text_image.size(); ++y) {
		for (size_t x = 0; x < text_image[0].size(); ++x) {
			render_char(charset[text_image[y][x].character],
				palette[text_image[y][x].fg_color],
				palette[text_image[y][x].bg_color], out,
				x * char_width, y * char_height);
		}
	}
	return out;
}

// TODO if I feel like it: rearrange characters so that adjacent characters
// are as similar as possible. Kinda gold-plating though...

void dump_charset(const Charset & characters, std::string filename) {
	std::ofstream out(filename);

	size_t char_height = characters[0].size(),
		char_width = characters[0][0].size(), i;

	if (char_width > 8) {
		throw std::logic_error("Can't dump charset > 8 wide!");
	}

	for (i = 0; i < characters.size(); ++i) {
		for (size_t j = 0; j < char_height; ++j) {
			unsigned char next_char = 0;
			for (size_t k = 0; k < 8; ++k) {
				if (characters[i][j][k]) {
					next_char += 1<< (7-k);
				}
			}
			out << next_char;
		}
	}

	for (i = characters.size(); i < 256; ++i) {
		for (size_t j = 0; j < 8; ++j) {
			out << (unsigned char)0;
		}
	}

	out.close();
}

void dump_video_memory(const TImage & text_image, std::string filename) {
	std::ofstream out(filename);

	for (size_t y = 0; y < text_image.size(); ++y) {
		for (size_t x = 0; x < text_image[0].size(); ++x) {
			if (text_image[y][x].character < 0 || text_image[y][x].character > 255) {
				throw std::logic_error("dump_video_memory: character index "
					" is not a byte!");
			}
			out << (unsigned char)(text_image[y][x].character);
			out << (unsigned char)(text_image[y][x].bg_color * 16 +
				text_image[y][x].fg_color);
		}
	}
	out.close();
}

void dump_palette(const Palette & palette, std::string filename) {
	std::ofstream out(filename);

	for (size_t i = 0; i < palette.size(); ++i) {
		// color order is Blue Green Red.
		out << (unsigned char)round(palette[i].b/255.0 * 63.0);
		out << (unsigned char)round(palette[i].g/255.0 * 63.0);
		out << (unsigned char)round(palette[i].r/255.0 * 63.0);
	}
	out.close();
}

// Count the number of 80x50 characters required for the caption.

size_t count_caption(const std::vector<std::string> caption,
	const Charset & charset_8x16) {

	std::set<DOSChar> already_seen;
	// Add space/NUL because we can fake that by setting foreground and
	// background colors equal.
	already_seen.insert(charset_8x16[0]);

	size_t uniques = 0;

	for (std::string str: caption) {
		for (char char_in_str: str) {
			// Check for upper and lower.
			for (DOSChar glyph : {charset_8x16[2*char_in_str],
				charset_8x16[2*char_in_str+1]}) {
				if (already_seen.find(glyph) == already_seen.end()) {
					++uniques;
					already_seen.insert(glyph);
				}
			}
		}
	}
	return uniques;
}

// Caption-related functions follow.

// Read an 8x16 charset dump into a std::vector<bool> as if it were a
// 512-character 8x8 charset. This is used to

Charset read_8x16_charset(std::string filename) {
	std::ifstream file(filename);

	Charset out = blank_charset(512, 8, 8);

	for (size_t i = 0; i < 512; ++i) {
		for (size_t j = 0; j < 8; ++j) {
			unsigned char encoded_line;
			file.read((char *)&encoded_line, 1);
			for (size_t k = 0; k < 8; ++k) {
				out[i][j][k] = (encoded_line & (1<<(7-k))) != 0;
			}
		}
	}

	return out;
}

void add_caption(const Charset & charset_8x16,
	TImage & quantized_image, Charset & charset_to_modify,
	Palette & palette, const std::vector<std::string> & caption,
	int caption_x, int caption_y) {

	// Add the final two colors (dark red and white) to the palette.

	if (palette.size() > 14) {
		throw std::runtime_error("add_caption: Too many colors in palette!");
	}

	size_t white = 0, red = 1;

	palette.insert(palette.begin(), pixel(170, 0, 0));		// dark red
	palette.insert(palette.begin(), pixel(255, 255, 255));

	std::map<DOSChar, int> caption_letters_idx;

	DOSChar space = charset_8x16[0];

	// Shift every existing color by 2 and every character by 1.
	// The reason we do this is so that the color white is 0, so that
	// color 00 will never be used and the color bit of the image (at least)
	// can be imported in KevEdit; and also so that character 00 is used only
	// once (for the same reason; we'll manually place it in assembler
	// code).

	// TODO: FIX BUG HERE! Somehow this doesn't get outputted into .chr...
	// DONE.

	char first_char = caption[0][0];
	//caption_letters_idx[charset_8x16[0]] = 0;
	caption_letters_idx[charset_8x16[first_char * 2]] = 0;
	//charset_to_modify.insert(charset_to_modify.begin(), charset_8x16[0]);
	charset_to_modify.insert(charset_to_modify.begin(), charset_8x16[first_char * 2]);

	for (size_t y = 0; y < quantized_image.size(); ++y) {
		for (size_t x = 0; x < quantized_image[y].size(); ++x) {
			quantized_image[y][x].fg_color += 2;
			quantized_image[y][x].bg_color += 2;
			++quantized_image[y][x].character;
		}
	}

	for (int line = 0; line < caption.size(); ++line) {
		for (int char_idx = 0; char_idx < caption[line].size(); ++char_idx) {
			char cur_char = caption[line][char_idx];

			// The glyphs for upper and lower part of the 8x16 character
			// respectively.
			std::vector<DOSChar> glyphs = {charset_8x16[cur_char * 2],
				charset_8x16[cur_char * 2 + 1]};

			// Note that caption_y, caption_x are relative to an 80x25
			// screen.
			int y = caption_y + line, x = caption_x + char_idx;

			for (size_t glyph_idx = 0; glyph_idx < 2; ++glyph_idx) {

				// If the glyph is a space, just simulate a space by
				 // setting foreground and background to the same color.
				if (glyphs[glyph_idx] == space) {
					// doesn't matter which
					quantized_image[y*2+glyph_idx][x].character = 1;
					quantized_image[y*2+glyph_idx][x].fg_color = red;
					quantized_image[y*2+glyph_idx][x].bg_color = red;
					continue;
				}

				// Otherwise...

				// Import the caption's letter if required.
				if (caption_letters_idx.find(glyphs[glyph_idx]) ==
					caption_letters_idx.end()) {

					caption_letters_idx[glyphs[glyph_idx]] = charset_to_modify.size();
					charset_to_modify.push_back(glyphs[glyph_idx]);
				}

				// Place the glyph in question.
				quantized_image[y*2+glyph_idx][x].character =
					caption_letters_idx[glyphs[glyph_idx]];
				quantized_image[y*2+glyph_idx][x].fg_color = white;
				quantized_image[y*2+glyph_idx][x].bg_color = red;
			}
		}
	}
}

// Need both upper and lower letter half for caption since this is 8x8.
// Also need two colors for white (for the letters) and red (background).

int main() {
	Image img = read_ppm("matrix_dithered.ppm");
	write_ppm(img, "out.ppm");

	std::string base_out = "TEST_out_matrix_dith_";

	// TODO: Don't count spaces.
	std::vector<std::string> caption = {
		"Whoa.",
		//"Apparently this is possible.",
		"Press a key to exit."};

	double record_img = DBL_MAX;

	Charset charset_8x16 = read_8x16_charset("cp437_8x16.chr");

	size_t caption_characters = count_caption(caption, charset_8x16),
		available_characters = 256 - caption_characters;

	size_t caption_x = 50, caption_y = 15;

	std::cout << "Subtracting " << caption_characters << " chars." << std::endl;

	for (int j = 0;; ++j) {

		std::string prefix = "imgout/" + base_out + itos(j);

		std::cout << "Trying iteration " << j << std::endl;

		srandom(j);
		srand48(j);

		Charset kmchar = blank_charset(available_characters, 8, 8);
		Palette kmpal(14);

		quant_error qerror;

		TImage test = quantize_image(img, kmchar, kmpal, qerror);
		Image imgout;

		imgout = render_image(test, kmchar, kmpal);
		std::cout << "Error: " << get_error(img, imgout) << std::endl;

		double noise = 0;
		double factor = drand48();

		int maxiters = 52;

		for (int i = 0; i < maxiters; ++i) {
			if (i < maxiters - 2) {
				noise = pow(factor, i); // Throw in some simulated annealing action
			} else {
				noise = 0; // but be greedy on the last two passes.
			}

			std::cout << j << ", " << i << "Error before: " << get_error(img, imgout) << std::endl;

			if (i % 2 == 0) {
				kmchar = update_charset(img, test, kmchar, kmpal, qerror, noise);
				test = quantize_image(img, kmchar, kmpal, qerror);

			} else {
				kmpal = update_palette(img, test, imgout, kmpal, noise * 0.75);
			}

			// With captions
			Palette kmpal_with_captions = kmpal;
			Charset kmchar_with_captions = kmchar;
			TImage img_with_captions = test;

			add_caption(charset_8x16, img_with_captions,
				kmchar_with_captions, kmpal_with_captions, caption,
				caption_x, caption_y);

			imgout = render_image(img_with_captions, kmchar_with_captions,
				kmpal_with_captions);

			double candidate_error = get_error(img, imgout);

			std::cout << j << ", " << i << "Error after: " << candidate_error << std::endl;
			if (candidate_error < record_img) {
				std::cout << "New record! " << j << ": " << candidate_error << std::endl;
				record_img = candidate_error;
				write_ppm(imgout, prefix + "_rec_" + itos(i) + ".ppm");
				dump_charset(kmchar_with_captions, prefix + "_rec_" + itos(i) +  ".chr");
				dump_video_memory(img_with_captions, prefix + "_rec_" + itos(i) + ".img");
				dump_palette(kmpal_with_captions, prefix + "_rec_" + itos(i) +  ".pal");
			}

			if (i == maxiters-1) {
				write_ppm(imgout, prefix + ".ppm");
				dump_charset(kmchar_with_captions, prefix + ".chr");
				dump_video_memory(img_with_captions, prefix + ".img");
				dump_palette(kmpal_with_captions, prefix + ".pal");
			}
			std::cout << std::endl;
		}
	}

	//write_ppm(imgout, "out2.ppm");
}
