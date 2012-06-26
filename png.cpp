#include "png.h"
#include <cstring>
#include <cassert>

unsigned char get_value(unsigned char *line, unsigned int i, unsigned char bit) {
  int mask = (1 << bit) -1;

  return (line[i*bit/8] >> ((16-bit-((i*bit) %8)) %8)) & mask;
}

//////////////////////////////////////////////////////////////////////////
//filter method 4 paeth predictor
inline unsigned char paeth_predictor(int a, int b, int c) {
    int p = a + b - c;
    int pa = p - a;
    int pb = p - b;
    int pc = p - c;
    if (pa<0) pa *= -1;
    if (pb<0) pb *= -1;
    if (pc<0) pc *= -1;

    unsigned char pr;

    if (pa <= pb && pa <= pc) pr = a;
    else if (pb <= pc) pr = b;
    else pr = c;

    return pr;
}

//////////////////////////////////////////////////////////////////////////
//constructor
PngFile::PngFile(void *input_data, unsigned int input_length) {
  unsigned int block_size = 0;

  width = 0;
  height = 0;
  background = 0;
  length = 0;
  scratch_size = 0;

  bit = 0;
  color = 0;
  interlace = 0;

  strm.zalloc = Z_NULL;
  strm.zfree = Z_NULL;
  strm.opaque = Z_NULL;
  strm.avail_in = 0;
  strm.next_in = Z_NULL;

  file = file_ptr = (unsigned char*) input_data;

  if (input_length < 20) return;

  //png header
  if (read_dword(0) != *(unsigned int*) "\x89PNG") return;
  if (read_dword(0) != *(unsigned int*) "\x0d\x0a\x1a\x0a") return;

  block_size = read_dword();
  if (block_size<13 || input_length-20 < block_size) return;

  if (read_dword(0) != *(unsigned int*) "IHDR") return;
  width = read_dword();
  height = read_dword();

  bit = *file_ptr++;
  if (bit>16) return;
  color = *file_ptr++;
  if (0 != *file_ptr++) return; //check compression type
  if (0 != *file_ptr++) return; //check filter type
  interlace = *file_ptr++;

  read_dword(); // skip checksum

  unsigned char channels[] = { 1,0,3,1,2,0,4 };

  if (color >= sizeof(channels) || channels[color] == 0) return;
  channel = channels[color];
  scratch_size = ((channel*width*bit+7)/8 + 1) * 2;

  // XXX test other invalid combinations
  if (bit>8&&color==3) return;

  while ((unsigned int) (file_ptr-file)+12 <= input_length) {
    block_size = read_dword();
    if (block_size > input_length-((file_ptr-file)+8) ) return;
    unsigned chunk = read_dword(0);

    if (chunk == *(unsigned int*) "IEND") {
      if (block_size) return;
      read_dword();
      length = (unsigned int) (file_ptr-file);
      reset();
      return;
    }

    //palette
    else if (chunk == *(unsigned int*) "PLTE") {
      if (block_size % 3) return;

      memcpy(pal,file_ptr,(block_size>768) ? 768 : block_size);
    }

    //background color
    else if (chunk == *(unsigned int*) "bKGD") {
      unsigned int req_bytes[] = {2,0,6,1,2,0,6};
      if (block_size!=req_bytes[color]) return;
      unsigned char *p = file_ptr;
      if (bit != 16) p++;
      switch (color) {
        case 0:
        case 4:
            background = *p * 0x010101;
        break;
        case 2:
        case 6:
            background = p[0]<<16 | p[2]<<8 | p[4];
        break;
        case 3:
          background = pal[*(p-1)][0] << 16 | pal[*(p-1)][1] << 8 | pal[*(p-1)][2];
        break;
      }
    }

    file_ptr += block_size;
    read_dword();
  }
}

//////////////////////////////////////////////////////////////////////////
//destructor
PngFile::~PngFile() {
  inflateEnd(&strm);
}

//////////////////////////////////////////////////////////////////////////
unsigned int PngFile::read_dword(bool be) {
  unsigned int val = *(unsigned int*) file_ptr;
  file_ptr += 4;
  if (be) return (val & 0xff000000) >> 24 |
                 (val & 0x00ff0000) >> 8  |
                 (val & 0x0000ff00) << 8  |
                 (val & 0x000000ff) << 24;
  return val;
}

//////////////////////////////////////////////////////////////////////////
//get next IDAT block
int PngFile::next_block() {
  unsigned int block_size = 0;

  while ((unsigned int) (file_ptr-file) < length) {
    block_size = read_dword();
    unsigned int chunk = read_dword(0);

    if (chunk == *(unsigned int*) "IEND") return 1;
    if (chunk == *(unsigned int*) "IDAT") {
      strm.avail_in = block_size;
      strm.next_in = file_ptr;
      file_ptr += block_size;
      read_dword();
      break;
    }
    else
     file_ptr += block_size+4;
  }
  if ((unsigned int) (file_ptr-file) >= length) return 1;
  return 0;
}

//////////////////////////////////////////////////////////////////////////
//decompress line
int PngFile::dec_line(unsigned char *line, unsigned int count) {
  int ret;
  strm.next_out = line;
  strm.avail_out = count;

  while (strm.avail_out > 0) {
    ret = 0;
    if (strm.avail_in == 0) ret = next_block();
    if (ret) return 2;

    ret = inflate(&strm, Z_NO_FLUSH);
    if (ret!=Z_OK && ret!=Z_STREAM_END) return 1;
  }
  return 0;
}


//////////////////////////////////////////////////////////////////////////
void PngFile::reset() {
  file_ptr = file+8;
  last_row = 0;
  last_pass = interlace ? 0 : 7;
  interlace_count = 0;
  inflateInit(&strm);
}

//////////////////////////////////////////////////////////////////////////
//read line
//return:
// -1 last row reached
// 1 zlib compression error
// 2 unexpected end of file
// 3 unknown filter type
int PngFile::read(void *row, bool use_bgrx, void *scratch) {
  unsigned char *line1 = (unsigned char*) scratch;
  unsigned char *line2 = (unsigned char*) scratch;
  if (interlace_count % 2)
    line1 += scratch_size/2;
  else
    line2 += scratch_size/2;

  unsigned char *row_ptr = (unsigned char*) row;

  int starting_row[]  = { 0, 0, 4, 0, 2, 0, 1, 0, 0 };
  int starting_col[]  = { 0, 4, 0, 2, 0, 1, 0, 0, 0 };
  int row_increment[] = { 8, 8, 8, 4, 4, 2, 2, 1, 0 };
  int col_increment[] = { 8, 8, 4, 4, 2, 2, 1, 1, 0 };

  unsigned int width2 = (width-starting_col[last_pass] + col_increment[last_pass] - 1 )/col_increment[last_pass];
  unsigned int line_len = (channel*width2*bit+7)/8 + 1;

  if (interlace_count == 0)
    memset(line1,0,line_len);

  //decompression
  int ret = dec_line(line2,line_len);
  if (ret) return ret;

  //defilter
  if (line2[0]>=5) return 3;
  int pixel_size = bit > 8 ? channel * 2 : channel;
  for (unsigned int x=1;x<line_len;x++) {
    int a = (x<pixel_size+1u) ? 0 : line2[x-pixel_size];
    int b = line1[x];
    int c = (x<pixel_size+1u) ? 0 : line1[x-pixel_size];
    switch (line2[0]) {
      case 1: line2[x] += a; break;
      case 2: line2[x] += b; break;
      case 3: line2[x] += (a + b) / 2; break;
      case 4: line2[x] += paeth_predictor(a,b,c); break;
    }
  }
  line2++;
  row_ptr += starting_col[last_pass] * ((use_bgrx) ? 4 : 3);
  unsigned char alpha = 0xff;

  //write row
  for (unsigned int i=0;i<width2;i++) {
    unsigned char val;
    switch (color) {
      case 0: //grayscale
        val = get_value(line2, i, bit);
        if (bit < 8)
           val *= 255/((1 << bit) -1);
        for (int j=0;j<3;j++)
          row_ptr[j] = val;
      break;

      case 2: //truecolor
        for (int j=0;j<3;j++)
          row_ptr[j] = line2[bit/8*(i*3 + 2-j)];
      break;

      case 3: //palette
        val = get_value(line2, i, bit);

        for (int j=0;j<3;j++)
          row_ptr[j] = pal[val][2-j];

      break;

      case 4: //grayscale + alpha
        for (int j=0;j<3;j++)
          row_ptr[j] = line2[i*bit/8*2];

        alpha = line2[bit/8*(i*2+1)];
      break;

      case 6: //truecolor + alpha
        for (int j=0;j<3;j++)
          row_ptr[j] = line2[bit/8*(i*4 + 2-j)];

        alpha = line2[bit/8*(i*4+3)];
      break;
    }
    if (use_bgrx) row_ptr[3] = alpha;
    row_ptr += col_increment[last_pass] * ((use_bgrx) ? 4 : 3);
  }


  last_row += row_increment[last_pass];
  interlace_count++;



  if (last_row >= height) {
    last_pass++;
    interlace_count = 0;
    last_row = starting_row[last_pass];

    if (last_pass >= 7) {
      reset();
      return -1;
    }
  }

  return 0;
}
