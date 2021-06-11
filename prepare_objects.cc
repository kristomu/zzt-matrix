#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <set>
#include <string>
#include <vector>

#include <assert.h>

// NOTE: integer labels prefixed with T are offsets from start of the tile area. These
// may differ based on the ZZT memory dump loaded, but may also need to be compared
// between different dumps to see if it's possible to craft an exploit that works on
// multiple ZZT versions. Hence they're integers instead of iterators.

// ----------------------------------------

// These are "reversed" because x86 is LSB.

struct ptr16 {
	uint16_t offset;
	uint16_t segment;
};

struct element_def {
	// Where the element definition in question starts in the DS dump.
	std::vector<char>::const_iterator definition_pos;

	// Some other pointers that we'll use when calculating where
	// duplicators or objects may be placed to overwrite the relevant
	// fields.

	std::vector<char>::const_iterator before_has_draw_proc,
		before_tick_proc, before_touch_proc;

	char character;						// +0
	unsigned char color;				// +1
	bool destructible;					// +2
	bool pushable;						// +3
	bool visible_in_dark;				// +4
	bool placeable_on_top;				// +5
	bool walkable;						// +6
	bool has_draw_proc;					// +7
	ptr16 draw_proc;					// +8
	int16_t cycle;						// +12
	ptr16 tick_proc;					// +14
	ptr16 touch_proc;					// +18
	int16_t editor_category;			// +22
	char editor_shortcut;				// +24
	std::string name;					// +25
	std::string category_name;			// +46
	std::string param_1_name;			// +67
	std::string param_2_name;			// +88
	std::string param_bullet_type_name;	// +109
	std::string param_board_name;		// +130
	std::string param_dir_name;			// +151
	std::string param_text_name;		// +172
	int16_t score_value;				// +193
										// +195
};

/* Weaver: everything from std::string name on down is the same.
   Looks like the only diff is that editor category and shortcut
   are gone. And for some reason, character and color are always ff
   ff (???). And some strings are gone: the whole thing has just len
   106.

   If two more bytes are gone, then we have 195-3-106-2 = 84, 84/21 = 4.

   Scroll has no auxiliary text, so param_text_name is gone.
   Passage has no auxiliary text, so param_board_name is gone.
   Duplicator has no auxiliary text, so param_2_name is gone.
   Object has Character? and Edit Program... so text name isn't gone?
   Slime has nothing, so param_2_name probably gone.
   But Shark has nothing either! Hm. Spinning Gun has nothing at all.

   For Object: we have "Object", then a void, then "Character?", then
   "Edit Program". That's Name, Category, Param 1, Text. Okay.

*/

const int MAX_STAT = 150;
const int MAX_ELEMENT = 53;

std::vector<char> fstream_to_vector(std::string filename) {
	char buffer[16384];

	std::ifstream in_file(filename);

	if (!in_file) {
		throw std::runtime_error("fstream_to_vector: Could not open file " +
			filename);
	}

	std::vector<char> file_contents;

	while (in_file) {
		in_file.read(buffer, 16384);
		std::copy(buffer, buffer+in_file.gcount(),
			std::back_inserter(file_contents));
	};

	return file_contents;
}

// least significant byte first
template<typename T> void read_integer(
	std::vector<char>::const_iterator & pos, T & out) {

	out = 0;
	T factor = 1;

	for (int i = 0; i < sizeof(T); ++i) {
		unsigned char p = *pos++;
		out += p * factor;
		factor *= 256;
	}
}

ptr16 get_pointer(std::vector<char>::const_iterator & pos) {
	ptr16 out;
	read_integer(pos, out.offset);
	read_integer(pos, out.segment);

	return out;
}

std::string get_string(std::vector<char>::const_iterator & pos,
	int buffer_length) {

	unsigned char length = *pos++;

	length = std::min(length, (u_char)buffer_length);

	std::string out;

	std::copy(pos, pos+length, std::back_inserter(out));
	pos += buffer_length;

	return out;
}

std::string get_element_def_string(std::vector<char>::const_iterator & pos) {
	return get_string(pos, 20); // length specified in GAMEVARS.PAS
}

std::vector<element_def> deserialize_element_defs(
	bool weaver_format, std::vector<char>::const_iterator pos,
	int how_many) {

	std::vector<element_def> definitions;

	for (int i = 0; i < how_many; ++i) {
		element_def next;

		next.definition_pos = pos;
		// I'm unsure about the first two here in Weaver.
		next.character = *pos++;
		next.color = *pos++;

		next.destructible = *pos++;
		next.pushable = *pos++;
		next.visible_in_dark = *pos++;
		next.placeable_on_top = *pos++;
		next.walkable = *pos++;

		next.before_has_draw_proc = pos;
		next.has_draw_proc = *pos++;
		next.draw_proc = get_pointer(pos);
		read_integer(pos, next.cycle);
		next.before_tick_proc = pos;
		next.tick_proc = get_pointer(pos);
		next.before_touch_proc = pos;
		next.touch_proc = get_pointer(pos);
		if (!weaver_format) {
			read_integer(pos, next.editor_category);
			next.editor_shortcut = *pos++;
		}

		next.name = get_element_def_string(pos);
		next.category_name = get_element_def_string(pos);
		next.param_1_name = get_element_def_string(pos);
		if (!weaver_format) {
			next.param_2_name = get_element_def_string(pos);
			next.param_bullet_type_name = get_element_def_string(pos);
			next.param_board_name = get_element_def_string(pos);
			next.param_dir_name = get_element_def_string(pos);
		}
		next.param_text_name = get_element_def_string(pos);
		// ???
		if (!weaver_format) {
			read_integer(pos, next.score_value);
		}

		definitions.push_back(next);
	}

	return definitions;
}

// Weaver ZZT uses a unique elements def.
std::vector<element_def> get_element_definitions_weaver(
	const std::vector<char> & ds_dump) {

	// Find the location of the elements table by searching for Empty.
	std::vector<char> to_find = {5, 'E', 'm', 'p', 't', 'y'};
	std::vector<char>::const_iterator empty_loc = std::search(
		ds_dump.begin(), ds_dump.end(),
		to_find.begin(), to_find.end());

	if (empty_loc == ds_dump.end()) {
		return {};
	}

	// Rewind 22 bytes to the start of the structure, and get all 256
	// entries (even, *especially* past the bounds of the array in Pascal,
	// because we may be able to use some of them for tricks later).
	return deserialize_element_defs(true, empty_loc - 22, 256);
}

// Get the element definitions for "ordinary" (2.0, 3.0, 3.2, 4.1) ZZT
// versions. See the comments above; this function is roughly analogous.
std::vector<element_def> get_element_definitions_zzt(
	const std::vector<char> & ds_dump) {

	std::vector<char> to_find = {0, 0, 5, 'E', 'm', 'p', 't', 'y'};
	std::vector<char>::const_iterator empty_loc = std::search(
		ds_dump.begin(), ds_dump.end(),
		to_find.begin(), to_find.end());

	if (empty_loc == ds_dump.end()) {
		return {};
	}

	return deserialize_element_defs(false, empty_loc - 23, 256);
}

std::vector<element_def> get_element_definitions(
	const std::vector<char> & ds_dump) {

	std::vector<element_def> element_defs;

	std::cout << "get_element_definitions: trying ZZT...\n";
	element_defs = get_element_definitions_zzt(ds_dump);
	if (!element_defs.empty()) {
		return element_defs;
	}
	std::cout << "get_element_definitions: trying Weaver...\n";
	element_defs = get_element_definitions_weaver(ds_dump);
	if (!element_defs.empty()) {
		return element_defs;
	}

	throw std::runtime_error("get_element_definitions: Could not"
		" find elements table in DS dump!");

}

std::string yes_no(bool boolean) {
	return boolean ? "Yes" : "No";
}

void print_element_defs(const std::vector<element_def> & element_defs,
	const std::vector<char>::const_iterator start) {

	for (size_t i = 0; i < element_defs.size(); ++i) {
		element_def element = element_defs[i];

		std::cout << "Element " << i;
		std::cout << "\tLocation:" << element.definition_pos - start << std::endl;
		std::cout << "\tDraw proc location:" << element.before_has_draw_proc + 1 - start << std::endl;
		std::cout << "HasDrawProc: " << yes_no(element.has_draw_proc) << std::endl;
		std::cout << "Visible in dark: " << yes_no(element.visible_in_dark) << std::endl;
		std::cout << "Pushable: " << yes_no(element.pushable) << std::endl;
		std::cout << "Draw proc: " << element.draw_proc.segment << ":" << element.draw_proc.offset << std::endl;
		std::cout << "Touch proc: " << element.touch_proc.segment << ":" << element.touch_proc.offset << std::endl;
		std::cout << "Tick proc: " << element.tick_proc.segment << ":" << element.tick_proc.offset << std::endl;
		std::cout << "----" << std::endl;
		std::cout << "Name:              " << element.name << std::endl;
		std::cout << "Category name:     " << element.category_name << std::endl;
		std::cout << "Param 1 name:      " << element.param_1_name << std::endl;
		std::cout << "Param 2 name:      " << element.param_2_name << std::endl;
		std::cout << "Bullet type name:  " << element.param_bullet_type_name << std::endl;
		std::cout << "Board name:        " << element.param_board_name << std::endl;
		std::cout << "Dir name:          " << element.param_dir_name << std::endl;
		std::cout << "Text name:         " << element.param_text_name << std::endl;

		std::cout << "----" << std::endl;
	}

}

// Checks if the byte representation of the integer given corresponds to a
// walkable or destructible element, i.e. that we can place stuff over it in
// memory by using a duplicator or similar.

bool is_byte_targetable(const std::vector<element_def> & element_defs,
	unsigned char element_idx) {

	// We pessimally assume that anything above the defined area (53)
	// is non-walkable (since 0xFF, the standard filler byte, is equivalent
	// to true and so would create a bunch of false positives otherwise).
	// Later use lots of different element defs and check if it's walkable
	// in all of them.

	return element_idx <= MAX_ELEMENT && (
		element_defs[element_idx].walkable || element_defs[element_idx].destructible);
}

bool is_integer_targetable(const std::vector<element_def> & element_defs,
	uint16_t integer_in_question) {

	// ZZT's element format is first byte is element, second is color.
	// Since it's LSB, the first byte corresponds to the value mod 256.

	return is_byte_targetable(element_defs, integer_in_question & 0xff);
}

bool is_pointer_targetable(const std::vector<element_def> & element_defs,
	ptr16 pointer) {

	return is_integer_targetable(element_defs, pointer.segment) &&
		is_integer_targetable(element_defs, pointer.offset);
}

// Check if all num_words from the position given by "starting" can be
// overwritten.
bool is_targetable(const std::vector<element_def> & element_defs,
	std::vector<char>::const_iterator starting, int num_words) {

	bool walkable = true;
	for (int i = 0; i < num_words && walkable; ++i) {
		walkable &= is_byte_targetable(element_defs, *starting);
		starting += 2;
	}

	return walkable;
}

// The board itself.
// Search for "Escape the Matrix".

std::vector<char>::const_iterator get_board_tiles_start(
	const std::vector<char> & ds_dump) {

	std::vector<char> to_find = {0x11, 'E', 's', 'c', 'a', 'p', 'e', ' ',
		't', 'h', 'e', ' ', 'M', 'a', 't', 'r', 'i', 'x'};
	std::vector<char>::const_iterator title_loc = std::search(
		ds_dump.begin(), ds_dump.end(),
		to_find.begin(), to_find.end());

	if (title_loc == ds_dump.end()) {
		throw std::runtime_error("get_board_tiles_start: Couldn't find board!");
	}

	return title_loc + 51;	// The board name is a TString50.
}

// Minimizes max(|x|, |y|) under the assumption that linear = 54 * x + 2 * y.
// Just some brute force so I don't have to import glpk.
std::pair<int, int> get_xy(int linear_distance) {

	int x = linear_distance / 54;
	int y = (linear_distance % 54) / 2;

	assert (54 * x + 2 * y == linear_distance);

	std::pair<int, int> record(x, y);
	int recordholder = std::max(abs(x), abs(y));

	while (x >= 0) {
		x -= 1;
		y += 27;

		assert (54 * x + 2 * y == linear_distance);

		if (std::max(abs(x), abs(y)) < recordholder) {
			record = std::pair<int, int>(x, y);
			recordholder = std::max(x, y);
		}
	}

	return record;
}

// Check if the given pointer type is a good candidate for being overwritten.
// We check for two possible types: even (the memory location containing the
// pointer starts with an element as interpreted as ZZT board data), and
// odd (starts with a color). If it's even, then all we need to do is copy
// two elements (corresponding to offset and segment); if odd, we need to copy
// three (whatever is before plus one byte of offset, the other byte of offset
// and one byte of segment, and the last byte of segment).

enum pointer_type { DRAW_PROC, TOUCH_PROC, TICK_PROC };

class walkable_pointer {
	public:
		size_t T_element_start; // relative to the start of tile data.
		pointer_type type;
		// If true, the pointer starts at element_start ("even"); if false, it
		// starts one later ("odd").
		bool pointer_at_element_start;

		std::string pointer_name(pointer_type type) const;
		void print() const;
};

std::string walkable_pointer::pointer_name(pointer_type type) const {
	switch(type) {
		case DRAW_PROC: return "DrawProc";
		case TOUCH_PROC: return "TouchProc";
		case TICK_PROC: return "TickProc";
		default:
			throw std::logic_error("pointer_name: unknown pointer type!");
	};
}

void walkable_pointer::print() const {

	if (pointer_at_element_start) {
		std::cout << pointer_name(type) << " (even):\t";
	} else {
		std::cout << pointer_name(type) << " (odd):\t";
	}

	std::pair<int, int> xy = get_xy(T_element_start);
	std::cout << T_element_start << "\t" << xy.first << ", " << xy.second << std::endl;
}

void add_walkable_info(const std::vector<element_def> & element_defs,
	pointer_type type, std::vector<char>::const_iterator tiles_start,
	std::vector<char>::const_iterator pointer_starts,
	std::vector<walkable_pointer> & add_to) {

	size_t distance = pointer_starts - tiles_start;

	walkable_pointer ptr;
	ptr.type = type;

	// If it's odd, the element preceding the pointer must be writable/walkable
	// (because the color is the first byte of the pointer), and the next byte must be
	// writable, as well as the third (because element is the last byte of the pointer).

	// If it's odd, add it.
	if (distance % 2 == 1 && is_targetable(element_defs, pointer_starts-1, 3)) {
		ptr.pointer_at_element_start = false;
		ptr.T_element_start = distance-1;
		add_to.push_back(ptr);
	}

	if (distance % 2 == 0 && is_targetable(element_defs, pointer_starts, 2)) {
		ptr.pointer_at_element_start = true;
		ptr.T_element_start = distance;
		add_to.push_back(ptr);
	}
}

// Candidate
// TickProc (odd):	20298	363, 348
// But it doesn't work in ZZT 2.0. Or 3.0.

// -----------------------------------------------------------------
// Description of the exploit attempt:

// Approach A: We want to copy a pointer to data that we control (most likely
// a pointer to an object's data) onto some element's Draw, Tick or TouchProc.
// Because we don't control the pointers themselves, the destination has to
// consist entirely of walkable elements when parsed as a board layout. (The
// alternative, which I'll call Approach B, is to just brute-force set every
// element to walkable).

// For an element memory area to be suitable, it must satisfy the following
// criteria:

//		- Both the draw proc offset and segment must be overwritable
//		- HasDrawProc must either be true (nonzero) or walkable must be
//			zero (so we can overwrite it and HasDrawProc).
//		- The distance between the start of the board tiles and the start
//			of the pointer must be even (so that a duplicator can be
//			placed somewhere in between).
//		- The xstep/ystep must be in range (not a problem as they're 16 bit)
//		- The x and y of the duplicator (midpoint) must be in range (8 bit)
//			and must be somewhere where we can set the bits to correspond
//			to a duplicator (most likely the object stats area unless I
//			want to get fancy).
//		- The pointer values must not change (this precludes the old
//			approach of force-setting element Timer (02) to be walkable
//			and then copying over Stats.

// I can't check the latter point except by running various versions of ZZT
// a lot and seeing if the values change.

// Additionally, the destination may be out of bounds, so that even a
// duplicator at 255,255 would fail to copy it over to 0,0. In that case we
// can use a trampoline technique: copy the source pointer to some location
// with negative coordinates, then copy from that location to the destination.

// We may even need a "double trampoline" that duplicates a duplicator onto (say)
// 255,255 (because we can't reach it directly), and then this duplicator duplicates
// some negative coordinate onto our destination.

const int E_DUPLICATOR = 12;

class zzt_memory {
	public:
		std::string ID;
		std::vector<char> ds_dump;
		std::vector<element_def> element_defs;
		std::vector<char>::const_iterator tiles_start;

		std::vector<walkable_pointer> walkable_pointers;

		size_t tiles_start_offset() const {
			return tiles_start - ds_dump.begin();
		}

		char at_T_ofs(size_t T_offset) const {
			return *(tiles_start + T_offset); }
};

zzt_memory get_zzt_memory(std::string filename) {
	zzt_memory output;

	output.ID = filename;
	output.ds_dump = fstream_to_vector(filename);
	output.element_defs = get_element_definitions(output.ds_dump);
	output.tiles_start = get_board_tiles_start(output.ds_dump);

	for (element_def element: output.element_defs) {
		add_walkable_info(output.element_defs, DRAW_PROC,
			output.tiles_start, element.before_has_draw_proc+1,
			output.walkable_pointers);
		add_walkable_info(output.element_defs, TICK_PROC,
			output.tiles_start, element.before_tick_proc,
			output.walkable_pointers);
		add_walkable_info(output.element_defs, TOUCH_PROC,
			output.tiles_start, element.before_touch_proc,
			output.walkable_pointers);
	}

	return output;
}
void print_element_defs(const zzt_memory & zm) {
	print_element_defs(zm.element_defs, zm.tiles_start);
}

class duplicator_solution {
	public:
		int T_source_pos, T_dupe_pos, T_dest_pos;

		duplicator_solution() {}
		duplicator_solution(int source, int dupe, int dest) {
			T_source_pos = source;
			T_dupe_pos = dupe;
			T_dest_pos = dest;
		}
};

// TODO: linear position to x,y pair so as to minimize some

// Find the placements of duplicators that copy from the starting location (source_pos)
// to the destination location (dest_pos).
// num_duplicators_reqd gives how many duplicators we want in total (the one specified and
// adjacent ones).
duplicator_solution get_duplicator_solution(const zzt_memory & search_in,
	int T_source_pos, int T_dest_pos, int num_duplicators_reqd) {

	// We can't copy something that's not element-aligned.
	if (T_source_pos % 2 != 0) {
		throw std::runtime_error("get_duplicator_solutions: not element-aligned!");
	}

	// maximum position where a duplicator may be placed without exceeding the bounds ot
	// the x and y parameters.
	int T_max_position = 255 * 54 + 255 * 2;

	// The duplicator position must be at the midpoint between the source and destination,
	// since it pivots the object to be copied about itself.

	// First check that this spot exists (i.e. is an integer).
	if ((T_dest_pos + T_source_pos) % 2 != 0) {
		throw std::runtime_error("get_duplicator_solutions: midpoint is not integer.");
	}

	int T_dupe_pos = (T_dest_pos + T_source_pos)/2;

	// It must also be even (so that it's an element) and within bounds.
	if (T_dupe_pos % 2 != 0 || T_dupe_pos < 0 || T_dupe_pos > T_max_position) {
		throw std::runtime_error("get_duplicator_solutions: unsuitable midpoint.");
	}

	// The duplicators must be placeable here.
	for (int dupe_num = 0; dupe_num < num_duplicators_reqd; ++dupe_num) {
		if (!is_byte_targetable(search_in.element_defs,
				search_in.at_T_ofs(T_dupe_pos + dupe_num * 2)) &&
			search_in.at_T_ofs(T_dupe_pos + dupe_num * 2) != E_DUPLICATOR) {
			throw std::runtime_error("get_duplicator_solutions: midpoint can't be overwritten");
		}
	}

	return duplicator_solution(T_source_pos, T_dupe_pos, T_dest_pos);
}

bool has_duplicator_solution(const zzt_memory & search_in,
	int T_source_pos, int T_dest_pos, int num_duplicators_reqd) {

	try {
		duplicator_solution x = get_duplicator_solution(search_in, T_source_pos,
			T_dest_pos, num_duplicators_reqd);
		return true;
	} catch (std::runtime_error & y) {
		//std::cout << y.what() << std::endl;
		return false;
	}
}

void find_duplicator_location(const zzt_memory & search_in,
	walkable_pointer first_element_to_overwrite) {

	for (int source_obj_idx = 0; source_obj_idx <= MAX_STAT; ++source_obj_idx) {
		int num_dupes = 2;		// number of duplicators needed
		int T_source_pos = 27 * 62 * 2 +				// number of tiles * bytes per tile
								2 + 					// StatCount
								source_obj_idx * 33	+	// TStats
								17;						// offset to ptr.

		if (!first_element_to_overwrite.pointer_at_element_start) {
			--T_source_pos;
			++num_dupes;
		}

		duplicator_solution solution;
		try {
			solution = get_duplicator_solution(search_in, T_source_pos,
				first_element_to_overwrite.T_element_start, num_dupes);
		} catch (std::runtime_error & e) {
			// It's not possible to fit a duplicator here; so continue.
			continue;
		}

		// Now use get_xy to get x, y and stepX, stepY. This won't find every alias but eh,
		// works reasonably well.
		std::pair<int, int> xy = get_xy(solution.T_dupe_pos);
		if (xy.first < 1 || xy.first > 255 | xy.second < 1 || xy.second > 255) {
			continue;
		}

		std::pair<int, int> step_x_y = get_xy(first_element_to_overwrite.T_element_start -
			solution.T_dupe_pos);

		std::cout << source_obj_idx << ": Found resolution: x,y = " << xy.first << ", " <<
			xy.second << " stepx, stepy: " << step_x_y.first << ", " << step_x_y.second <<
			" to " << first_element_to_overwrite.T_element_start << "\n";
	}

	std::cout << "Job's done!" << std::endl;
}

void find_indirect_duplicator_locations(const zzt_memory & search_in,
	walkable_pointer first_element_to_overwrite) {

	// A.2. If we can't reach the destination directly because it'd require a duplicator
	// with (x,y) > 255, then we can try to copy from the source (some object's data pointer)
	// to a temporary holding position at a negative T offset, so that the xstep,ystep will
	// pivot further.

	// The holding position must necessarily be before the tiles for xstep,ystep to magnify
	// (i.e. reach later into memory).

	for (int source_obj_idx = 0; source_obj_idx <= MAX_STAT; ++source_obj_idx) {
		int T_source_pos = 27 * 62 * 2 +				// number of tiles * bytes per tile
								2 + 					// StatCount
								source_obj_idx * 33	+	// TStats
								17;						// offset to ptr.
		int num_dupes = 2;
		int tiles_start_at = search_in.tiles_start_offset();

		if (!first_element_to_overwrite.pointer_at_element_start) {
			--T_source_pos;
			++num_dupes;
		}

		// must be element-aligned
		if (T_source_pos % 2 != 0) {
			continue;
		}

		// Since (T_dest_pos + T_source_pos) % 2 must be 0, and T_source_pos is 0 mod 2,
		// so must T_dest_pos also.
		while (tiles_start_at % 2 != 0) { --tiles_start_at; }

		for (int holding_position = -tiles_start_at; holding_position < 0; holding_position += 2) {
			if (!has_duplicator_solution(search_in, T_source_pos,
				holding_position, num_dupes)) {
				continue;
			}

			if (!has_duplicator_solution(search_in, holding_position,
				first_element_to_overwrite.T_element_start, num_dupes)) {
				continue;
			}

			duplicator_solution source_to_negative = get_duplicator_solution(search_in,
				T_source_pos, holding_position, num_dupes);
			duplicator_solution negative_to_dest = get_duplicator_solution(search_in,
				holding_position, first_element_to_overwrite.T_element_start, num_dupes);

			std::cout << "Indirect: " << source_obj_idx << ", " << T_source_pos << " -[" << source_to_negative.T_dupe_pos <<
				"]-> " << holding_position << " -[" << negative_to_dest.T_dupe_pos << "]-> "
				<< first_element_to_overwrite.T_element_start << std::endl;
		}
	}
}

void dupe_P2(const zzt_memory & search_in) {

	int source_obj_idx = 1;

	int T_source_pos = 27 * 62 * 2 +	// number of tiles * bytes per tile
		2 + 							// StatCount
		source_obj_idx * 33	+			// TStats
		9;								// offset to P2

	for (int destX = 1; destX < 60; ++destX) {
		for (int destY = 1; destY < 25; ++destY) {
			duplicator_solution soln;
			try {
				soln = get_duplicator_solution(search_in, T_source_pos, destX * 54 + destY * 2,
					1);
				std::pair<int, int> xy = get_xy(soln.T_dupe_pos);
				if (xy.first < 1 || xy.first > 60 || xy.second < 1 || xy.second > 25) {
					continue;
				}

				std::cout << "Found solution: " << soln.T_dupe_pos << std::endl;
				std::cout << "x, y: " << xy.first << ", " << xy.second << std::endl;
				std::pair<int, int> step = get_xy(soln.T_dest_pos - soln.T_dupe_pos);
				std::cout << "stepX, stepY: " << -step.first << ", " << -step.second << std::endl;
				std::cout << "Destination: x, y: " << destX << ", " << destY << std::endl << std::endl;
			} catch (std::runtime_error & x) {
			}
		}
	}
}

int main(int argc, char * * argv) {

	size_t i;

	if (argc < 2) {
		std::cout << "Usage: " << argv[0] << " [ZZT data segment dump] ... [ZZT data segment dump]"
			<< std::endl;
		return -1;
	}

	zzt_memory zm = get_zzt_memory(argv[1]);

	std::vector<char> ds_dump = zm.ds_dump;
	std::vector<element_def> element_defs = zm.element_defs;

	std::cout << "Who will save the widow's son?" << std::endl;

	std::vector<char>::const_iterator tiles_start = zm.tiles_start;

	std::cout << "Offset to tiles: " << zm.ds_dump.begin() - tiles_start << std::endl;
	std::cout << "Possible object walk destinations: " << std::endl;

	std::string printable_targetable = "";

	for (char i = 0; i <= MAX_ELEMENT; ++i) {
		if (is_byte_targetable(zm.element_defs, i) && isprint(i)) {
			printable_targetable += i;
		}
	}

	std::cout << "Printable targetable elements: " << printable_targetable << "\n";

	for (walkable_pointer ptr: zm.walkable_pointers) {
		ptr.print();
	}

	//print_element_defs(zm);

	// Strategy A.1.: Find a location in memory that corresponds to a walkable element,
	// and a step size so that two or three duplicator placed around that location can
	// copy the pointer of some object onto DrawProc, TickProc or TouchProc of some
	// unused element.

	// For this approach to work, the duplicators to be placed must all be at x,y
	// <= 255, 255, because we need to set stats on them to duplicate from the proper
	// source to the proper destination; and stats can't handle x or y > 255.

	// Thus the destination must be < 512, 512 because a duplicator placed at 255, 255 with
	// stepsize 255, 255 will copy from 0,0; and the stats data pointers are all found
	// *after* the tile data. Hence the source coordinates will be positive.

	// variables starting in T are relative offsets with 0 being start of tile data.

	/*for (walkable_pointer ptr: zm.walkable_pointers) {
		std::cout << "Trying: ";
		ptr.print();
		find_duplicator_location(zm, ptr);
	}*/

	// Empirical testing shows that the one interesting location is actually part of the
	// timer variables and so halts the game if we try to store a pointer in there.
	// The alternative is using a prepared high-score file to zero out enough space to
	// place a pointer in, but that's kinda kludgy, dontchathink?

	// A.2. Test if there exists a negative position containing zeroes so that we can copy
	// from the source (stats position) to the negative position and then back to the
	// destination.
	/*for (walkable_pointer ptr: zm.walkable_pointers) {
		std::cout << "Trying: ";
		ptr.print();
		find_indirect_duplicator_locations(zm, ptr);
	}*/

	// Indirect: 1, 3400 -[238]-> -2924 -[11512]-> 25948

	// So getting something from object 1 to 25948 involves:
	//		A duplicator at 238 copying from 3400 to -2924
	//		A duplicator at 11512 copying from -2924 to 25948
	// So let's find out what the x,y xstep,ystep numbers for these are.
	// (Offset to tile start: 9405)

	std::pair<int, int> xy = get_xy(238);
	std::cout << "First duplicator at " << xy.first << ", " << xy.second << std::endl;
	std::pair<int, int> step = get_xy(3400-238);
	std::cout << "Step: x " << step.first << ", " << step.second << std::endl;
	xy = get_xy(11512);
	std::cout << "Second duplicator at " << xy.first << ", " << xy.second << std::endl;
	step = get_xy(25948-11512);
	std::cout << "Step: x " << -step.first << ", " << -step.second << std::endl;
	// Need to set visible in dark.
	step = get_xy(25944);
	std::cout << "Visible in dark: Target: x" << step.first << ", " << step.second << std::endl;

	// TBD: Implement. Implement.

	// Finally, find a duplicator that can copy the element we want onto the playing field for
	// display.
	dupe_P2(zm);

	return 0;
}