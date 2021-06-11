#include <iostream>
#include <fstream>
#include <vector>

// Render palette and character data into either assembler db statements or
// a proper ZZT board file (later).

// https://stackoverflow.com/a/2602885
unsigned char reverse(unsigned char b) {
   b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
   b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
   b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
   return b;
}

void print_vector_asm(const std::vector<char> & input, int max_length, bool reversed) {
	for (size_t i = 0; i < input.size(); i += max_length) {
		if (i != 0) {
			std::cout << ", 13" << "\ndb ";
		} else {
			std::cout << "db ";
		}
		for (size_t j = i; j < std::min(input.size(), i + max_length); ++j) {
			int cur_byte = (unsigned char)input[j];
			if (reversed) {
				cur_byte = reverse(cur_byte);
			}
			if (j != i) {
				std::cout << ", ";
			}
			std::cout << "0" << std::hex << cur_byte << "h";
		}
	}
	std::cout << std::endl;
}

void print_vector_to_file(const std::vector<char> & input, const std::string out_filename,
	std::string label, int max_length) {

	std::ofstream outfile(out_filename);
	outfile << "@" << label << "\r#end\r";

	char newline = 13, one = 1;

	for (size_t i = 0; i < input.size(); i += max_length) {
		if (i != 0) {
			outfile.write(&newline, 1);
		}
		for (size_t j = i; j < std::min(input.size(), i + max_length); ++j) {
			if (input[j] == 0) {
				std::cout << "print_vector_to_file: writing " << out_filename << ": NUL at "
					<< j << ", writing 0x1 instead.\n";
					outfile.write(&one, 1);
			} else {
				outfile.write(&input[j], 1);
			}
		}
	}
}

// Very cut and paste but whatever
std::vector<char> read_file(const std::string filename) {
	std::vector<char> out;
	std::ifstream file(filename);

	// https://stackoverflow.com/a/7242025
	file.seekg(0, std::ios_base::end);
	std::streampos file_size = file.tellg();
	out.resize(file_size);

	file.seekg(0, std::ios_base::beg);
	file.read(&out[0], file_size);

	return out;
}

// Some tricks:
// The .chr file will inevitably have some NUL values, which KevEdit really doesn't
// like. We thus need to XOR with some value that doesn't exist in the file so that
// there are no NULs, and XOR back inside the ZZT payload.

void print_chr_file(const std::vector<char> & char_data,
	const std::string out_filename, std::string label, int max_length) {

	std::vector<int> character_count(256, 0);
	size_t i;

	for (i = 0; i < char_data.size(); ++i) {
		character_count[(unsigned char)(char_data[i])]++;
	}

	int unseen_character = -1;
	for (i = 0; i < character_count.size() && unseen_character == -1; ++i) {
		if (character_count[i] == 0) {
			unseen_character = i;
		}
	}

	if (unseen_character == -1) {
		throw std::runtime_error("print_chr_file: "
			"Could not find unused byte to XOR char data with.");
	} else {
		std::cout << "print_chr_file: XOR constant for chr file is " << unseen_character << std::endl;
	}

	std::vector<char> modified_data;

	for (char x : char_data) {
		modified_data.push_back(x ^ (char)unseen_character);
	}

	print_vector_to_file(modified_data, out_filename, label, max_length);
}

// The .img file has been engineered to have exactly one NUL byte. This zero can be
// replaced with some arbitrary byte (I'll probably use 0xFF) and then we just have
// to set that zero byte separately, so the program should report where it is.

void print_img_file(std::vector<char> img_data, const std::string out_filename,
	std::string label, int max_length) {

	size_t i, zero_location = -1;

	for (i = 0; i < img_data.size(); ++i) {
		if (img_data[i] != 0) { continue; }

		if (zero_location != -1) {
			throw std::runtime_error("print_img_file: multiple zeroes detected!");
		}
		zero_location = i;
		img_data[i] = -1;
	}

	std::cout << "print_img_file: zero byte at " << zero_location << std::endl;
	print_vector_to_file(img_data, out_filename, label, max_length);
}

// For .pal files, we can add 0x40 to each byte because the bytes must range from
// 0 to 0x3F inclusive.
void print_pal_file(std::vector<char> pal_data, const std::string out_filename,
	std::string label, int max_length) {

	for (size_t i = 0; i < pal_data.size(); ++i) {
		if (pal_data[i] > 0x3F) {
			throw std::runtime_error("print_pal_file: palette value out of range.");
		}
		if (pal_data[i] == 0) {
			pal_data[i] += 0x40;
		}
	}

	print_vector_to_file(pal_data, out_filename, label, max_length);
}

int main() {
	std::string prefix = "matrix"; // "dodeca_8x8_0"; //"chosen/matrix";

	std::vector<char> palette = read_file(prefix + ".pal");
	std::vector<char> charset = read_file(prefix + ".chr");
	std::vector<char> image = read_file(prefix + ".img");

	std::cout << "palette:" << std::endl;
	//print_vector_asm(palette, 42, false);
	print_pal_file(palette, "pal_" + prefix + ".zoc", "XX Palette", 3*8);
	std::cout << std::endl << "charset:" << std::endl;
	//print_vector_asm(charset, 40, false);
	print_chr_file(charset, "chr_" + prefix + ".zoc", "XX Charset", 40);
	std::cout << std::endl << "image:" << std::endl;
	//print_vector_asm(image, 40, false);
	print_img_file(image, "img_" + prefix + ".zoc", "XX Picture", 40);

	return 0;
}