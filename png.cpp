#include "png.h"
#include <cstring>
#include <cassert>

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
  last_row = 0;
  last_pass = 0;

  bit = 0;
  color = 0;
  interlace = 0;

  line_len = 0;
  pixel_size = 0;

  pal_number = 0;
  interlace_count = 0;

  strm.zalloc = Z_NULL;
  strm.zfree = Z_NULL;
  strm.opaque = Z_NULL;
  strm.avail_in = 0;
  strm.next_in = Z_NULL;
  inflateInit(&strm);

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

  unsigned char pixel_sizes[] = { 1,0,3,1,2,0,4 };

  if (color >= sizeof(pixel_sizes) || pixel_sizes[color] == 0) return;
  pixel_size = pixel_sizes[color];
  if (bit == 16) pixel_size *= 2;

  if (color != 3)
    line_len = width*pixel_size+1;
  else {
    if (bit>8) return;
    line_len = (width*bit % 8) ? width*bit/8 +2 : width*bit/8 +1;
    pal_number = 1 << bit;

    //initialize palette with gray scale
    for (int i=0;i<pal_number;i++) {
      unsigned char temp = 255*i/(pal_number-1);
      pal[i] = temp<<16 | temp<<8 | temp;
    }
  }

  scratch_size = line_len * 2;

  while ((unsigned int) (file_ptr-file)+12 <= input_length) {
    block_size = read_dword();
    if (block_size > input_length-((file_ptr-file)+8) ) return;
    unsigned chunk = read_dword(0);

    if (chunk == *(unsigned int*) "IEND") {
      if (block_size) return;
      read_dword();
      length = (unsigned int) (file_ptr-file);
      file_ptr = file + 8;
      return;
    }

    //palette
    else if (chunk == *(unsigned int*) "PLTE") {
      if (block_size!=pal_number*3) return;

      for (int i=0;i<pal_number;i++)
        pal[i] = file_ptr[i*3]<<16 | file_ptr[i*3+1]<<8 | file_ptr[i*3+2];

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
          background = pal[*(p-1)];
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
    if (ret) return 1;
  }
  return 0;
}

//////////////////////////////////////////////////////////////////////////
//read line interlaced
int PngFile::read_interlaced(void *row, bool use_bgrx, void *scratch) {
  unsigned char *line1 = (unsigned char*) scratch;
  unsigned char *line2 = (unsigned char*) scratch;
  if (interlace_count % 2)
    line1 += line_len;
  else
    line2 += line_len;

  unsigned char *row_ptr = (unsigned char*) row;
  int ret = 0;
  int type = 0;
  unsigned int line_len2 = 0;

  if (last_row >= height || last_pass >= 7) return -1;


  int starting_row[8]  = { 0, 0, 4, 0, 2, 0, 1, 0 };
  int starting_col[8]  = { 0, 4, 0, 2, 0, 1, 0, 0 };
  int row_increment[8] = { 8, 8, 8, 4, 4, 2, 2, 0 };
  int col_increment[8] = { 8, 8, 4, 4, 2, 2, 1, 0 };

  unsigned int width2 = width-starting_col[last_pass];
  width2 = (width2%col_increment[last_pass])
          ? width2/col_increment[last_pass] +1
          : width2/col_increment[last_pass];

    if(bit < 8)
      line_len2 = (width2*bit % 8) ? width2*bit/8 +2 : width2*bit/8 +1;
    else
      line_len2 = width2*pixel_size+1;

  if (interlace_count == 0)
    memset(line1,0,line_len);



  //decompression
  ret = dec_line(line2,line_len2);
  if (ret) return ret;

  //defilter
  type = line2[0];
  for (unsigned int x=1;x<line_len2;x++) {
    int a = (x<(unsigned int) pixel_size+1) ? 0 : line2[x-pixel_size];
    int b = line1[x];
    int c = (x<(unsigned int) pixel_size+1) ? 0 : line1[x-pixel_size];
    switch (type) {
      case 1: line2[x] += a; break;
      case 2: line2[x] += b; break;
      case 3: line2[x] += (a + b) / 2; break;
      case 4: line2[x] += paeth_predictor(a,b,c); break;
    }
  }

  line2++;
  row_ptr += starting_col[last_pass]* ((use_bgrx) ? 4 : 3);

  //write row
  switch (color) {
    case 0:
      for (unsigned int i=0;i<width2;i++) {
        for (int j=0;j<3;j++)
          row_ptr[j] = *line2;

        if (use_bgrx) row_ptr[3] = 0xff;
        line2++;
        if (bit==16) line2++;

        row_ptr += col_increment[last_pass] * ((use_bgrx) ? 4 : 3);
      }
    break;
    case 2:
      for (unsigned int i=0;i<width2;i++) {
        for (int j=2;j>=0;j--) {
          row_ptr[j] = *line2++;
          if (bit==16) line2++;
        }
        if (use_bgrx) row_ptr[3] = 0xff;

        row_ptr += col_increment[last_pass] * ((use_bgrx) ? 4 : 3);
      }
    break;

    case 3:
      int jump;
      int and;
      switch(bit) {
        case 1:
          jump = 8;
          and = 1;
        break;
        case 2:
          jump = 4;
          and = 3;
        break;
        case 4:
          jump = 2;
          and = 15;
        break;
        case 8:
          jump = 1;
          and = 255;
        break;
      }
      for (unsigned int i=0;i<width2;i+=jump) {
        unsigned char temp = *line2++;
        for (int j=(jump-1);j>=0;j--) {
          for (int k=2;k>=0;k--)
            row_ptr[k] = (pal[(temp>>(j*bit))&and]>>(k*8)) & 0xff;
          if (use_bgrx) row_ptr[3] = 0xff;

          row_ptr += col_increment[last_pass] * ((use_bgrx) ? 4 : 3);
        }
      }
    break;

    case 4:
      for (unsigned int i=0;i<width2;i++) {
        for (int j=0;j<3;j++)
          row_ptr[j] = *line2;
        line2 += ((bit==16) ? 2 : 1);

        if (use_bgrx) row_ptr[3] = *line2;
        line2 += ((bit==16) ? 2 : 1);

        row_ptr += col_increment[last_pass] * ((use_bgrx) ? 4 : 3);
      }
    break;

    case 6:
      for (unsigned int i=0;i<width2;i++) {
        for (int j=2;j>=0;j--) {
          row_ptr[j] = *line2++;
          if (bit==16) line2++;
        }
        if (use_bgrx) {
          row_ptr[3] = *line2++;
        }
        else {
          line2++;
        }
        if (bit==16) line2++;
        row_ptr += col_increment[last_pass] * ((use_bgrx) ? 4 : 3);
      }
    break;
  }

  last_row += row_increment[last_pass];
  interlace_count++;
  if (last_row >= height) {
    last_pass++;
    interlace_count = 0;
    last_row = starting_row[last_pass];
  }

  return 0;
}


//////////////////////////////////////////////////////////////////////////
//read line
//return:
// -1 last row already reached
// 1 zlib compression error
// 2 unexpected end of file
int PngFile::read(void *row, bool use_bgrx, void *scratch) {
  if (interlace) return read_interlaced(row, use_bgrx, scratch);

  unsigned char *line1 = (unsigned char*) scratch;
  unsigned char *line2 = (unsigned char*) scratch;
  if (last_row % 2)
    line1 += line_len;
  else
    line2 += line_len;


  unsigned char *row_ptr = (unsigned char*) row;
  int ret = 0;
  int type = 0;

  if (last_row >= height) return -1;


  if (last_row == 0)
    memset(line1,0,line_len);


  //decompression
  ret = dec_line(line2,line_len);
  if (ret) return ret;

  //defilter
  type = line2[0];
  for (unsigned int x=1;x<line_len;x++) {
    int a = (x<(unsigned int) pixel_size+1) ? 0 : line2[x-pixel_size];
    int b = line1[x];
    int c = (x<(unsigned int) pixel_size+1) ? 0 : line1[x-pixel_size];
    switch (type) {
      case 1: line2[x] += a; break;
      case 2: line2[x] += b; break;
      case 3: line2[x] += (a + b) / 2; break;
      case 4: line2[x] += paeth_predictor(a,b,c); break;
    }
  }

  line2++;

  //write row
  switch (color) {
    case 0:
      for (unsigned int i=0;i<width;i++) {
        for (int j=0;j<3;j++)
          *row_ptr++ = *line2;

        if (use_bgrx) *row_ptr++ = 0xff;
        line2+= ((bit==16) ? 2 : 1);
      }
    break;
    case 2:
      for (unsigned int i=0;i<width;i++) {
        for (int j=2;j>=0;j--) {
          row_ptr[j] = *line2++;
          if (bit==16) line2++;
        }
        if (use_bgrx) {
          row_ptr[3] = 0xff;
          row_ptr+=4;
        }
        else
          row_ptr+=3;
      }
    break;

    case 3:
      int jump;
      int and;
      switch(bit) {
        case 1:
          jump = 8;
          and = 1;
        break;
        case 2:
          jump = 4;
          and = 3;
        break;
        case 4:
          jump = 2;
          and = 15;
        break;
        case 8:
          jump = 1;
          and = 255;
        break;
      }
      for (unsigned int i=0;i<width;i+=jump) {
        unsigned char temp = *line2++;
        for (int j=(jump-1);j>=0;j--) {
          for (int k=2;k>=0;k--)
            row_ptr[k] = (pal[(temp>>(j*bit))&and]>>(k*8)) & 0xff;
          if (use_bgrx) {
            row_ptr[3] = 0xff;
            row_ptr+=4;
          }
          else
            row_ptr+=3;
        }
      }
    break;

    case 4:
      for (unsigned int i=0;i<width;i++) {
        for (int j=0;j<3;j++)
          *row_ptr++ = *line2;
        line2++;
        if (bit==16) line2++;

        if (use_bgrx) *row_ptr++ = *line2++;
        else line2++;

        if (bit==16) line2++;
      }
    break;

    case 6:
      for (unsigned int i=0;i<width;i++) {
        for (int j=2;j>=0;j--) {
          row_ptr[j] = *line2++;
          if (bit==16) line2++;
        }
        if (use_bgrx) {
          row_ptr[3] = *line2++;
          row_ptr+=4;
        }
        else {
          row_ptr+=3;
          line2++;
        }
        if (bit==16) line2++;
      }
    break;
  }
  last_row++;


  return 0;
}

