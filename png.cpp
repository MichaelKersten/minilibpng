#include "png.h"
#include <iostream>

//////////////////////////////////////////////////////////////////////////
//Endian conversion
inline void endian(unsigned int *val) {
  *val = (*val & 0xff000000) >> 24 |
         (*val & 0x000000ff) << 24 |
         (*val & 0x00ff0000) >> 8 |
         (*val & 0x0000ff00) << 8;
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
  last_row = 0;
  last_pass = 0;

  bit = 0;
  color = 0;
  comp = 0;
  filter = 0;
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

  //png header
  if (strncmp((char*) file_ptr,"\x89PNG\x0d\x0a\x1a\x0a",8)) return;
  file_ptr += 12;
  if (strncmp((char*) file_ptr,"IHDR",4)) return;
  file_ptr+=4;
  width = *(unsigned int*) file_ptr; file_ptr+=4;
  endian(&width);
  height = *(unsigned int*) file_ptr; file_ptr+=4;
  endian(&height);

  bit = *file_ptr++;
  color = *file_ptr++;
  comp = *file_ptr++;
  filter = *file_ptr++;
  interlace = *file_ptr++;

  file_ptr += 4;

  switch(color) {
    case 0: //gray scale
    case 3: //palette
      line_len = (width*bit % 8) ? width*bit/8 +2 : width*bit/8 +1;
      if (bit==16) pixel_size = 2;
      else pixel_size = 1;

      //initialize palette with gray scale
      switch (bit) {
        case 1: pal_number = 2; break;
        case 2: pal_number = 4; break;
        case 4: pal_number = 16; break;
        case 8: pal_number = 256; break;
      }
      for (int i=0;i<pal_number;i++) {
        unsigned char temp = 255*i/(pal_number-1);
        pal[i] = 0xff000000 | temp<<16 | temp<<8 | temp;
      }

    break;
    case 2: //true color
      if (bit==16) pixel_size = 6;
      else pixel_size = 3;
      line_len = width*pixel_size+1;
    break;
    case 4: //gray scale with alpha
      if (bit==16) pixel_size = 4;
      else pixel_size = 2;
      line_len = width*pixel_size+1;
    break;
    case 6: //true color width alpha
      if (bit==16) pixel_size = 8;
      else pixel_size = 4;
      line_len = width*pixel_size+1;
    break;
  }

  scratch_size = line_len * 2;
  length = input_length;

  while ((unsigned int) (file_ptr-file) < length) {
    block_size = *(unsigned int*) file_ptr; file_ptr += 4;
    endian(&block_size);

    if (0 == strncmp((char*) file_ptr,"IEND",4)) break;

    //palette
    if (0 == strncmp((char*) file_ptr,"PLTE",4)) {
      file_ptr += 4;

      for (int i=0;i<pal_number;i++)
        pal[i] = file_ptr[i*3]<<16 | file_ptr[i*3+1]<<8 | file_ptr[i*3+2];

      file_ptr += block_size+4;
      continue;
    }

    //background color
    if (0 == strncmp((char*) file_ptr,"bKGD",4)) {
      file_ptr += 4;

      switch (color) {
        case 0:
        case 4:
          if (bit == 16)
            background = file_ptr[0]<<16 | file_ptr[0]<<8 | file_ptr[0];
          else
            background = file_ptr[1]<<16 | file_ptr[1]<<8 | file_ptr[1];

        break;
        case 2:
        case 6:
          if (bit == 16)
            background = file_ptr[0]<<16 | file_ptr[2]<<8 | file_ptr[4];
          else
            background = file_ptr[1]<<16 | file_ptr[3]<<8 | file_ptr[5];
        break;
        case 3:
          background = pal[file_ptr[0]];
        break;
      }
      file_ptr += block_size+4;
      continue;
    }
    file_ptr += block_size+8;
  }

  file_ptr = file + 8;
}

//////////////////////////////////////////////////////////////////////////
//destructor
PngFile::~PngFile() {
  inflateEnd(&strm);

}

//////////////////////////////////////////////////////////////////////////
//get next IDAT block
int PngFile::next_block() {
  unsigned int block_size = 0;

  while ((unsigned int) (file_ptr-file) < length) {
    block_size = *(unsigned int*) file_ptr; file_ptr += 4;
    endian(&block_size);

    if (0 == strncmp((char*) file_ptr,"IEND",4)) return 1;
    if (0 == strncmp((char*) file_ptr,"IDAT",4)) {
      file_ptr+=4;
      strm.avail_in = block_size;
      strm.next_in = file_ptr;
      file_ptr += block_size+4;
      break;
    }
    else
     file_ptr += block_size+8;
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
    if (ret) {
      if (ret == Z_DATA_ERROR || ret == Z_MEM_ERROR) return 1;

      if (ret == Z_NEED_DICT) strm.avail_in = 0;
    }
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












/*


//////////////////////////////////////////////////////////////////////////
//PNG Daten deinterlacen
void deinterlace(unsigned char* str, PNG_Header png_head, int zeile, int pixel, int str_groesse) {
  unsigned char* temp = NULL;
  unsigned char* temppos = NULL;
  unsigned char* temp_bild = NULL;
  unsigned char pix_wert;

  int starting_row[7]  = { 0, 0, 4, 0, 2, 0, 1 };
  int starting_col[7]  = { 0, 4, 0, 2, 0, 1, 0 };
  int row_increment[7] = { 8, 8, 8, 4, 4, 2, 2 };
  int col_increment[7] = { 8, 8, 4, 4, 2, 2, 1 };

  temp = new unsigned char[str_groesse];
  temppos = temp;
  temp_bild = new unsigned char[str_groesse];
  for (int i=0;i<str_groesse;i++) {temp[i] = str[i]; str[i] = 0;}

  for (int pass = 0;pass<7;pass++) {
    int x2 = 0;
    int y2 = 0;
    int br2 = ((png_head.Breite-starting_col[pass])%col_increment[pass])
             ? (png_head.Breite-starting_col[pass])/col_increment[pass] +1
             : (png_head.Breite-starting_col[pass])/col_increment[pass];
    int ho2 = ((png_head.Hoehe-starting_row[pass])%row_increment[pass])
              ? (png_head.Hoehe-starting_row[pass])/row_increment[pass] +1
              : (png_head.Hoehe-starting_row[pass])/row_increment[pass];
    int zeile2;


    if(png_head.Bit < 8) {
      zeile2 = (br2*png_head.Bit % 8) ? br2*png_head.Bit/8 +2 : br2*png_head.Bit/8 +1;
    }
    else zeile2 = br2*pixel+1;

    for (int i=0;i<zeile2*ho2;i++) temp_bild[i] = *temppos++;

    defilter(temp_bild, ho2, zeile2, pixel);


    for (int y = starting_row[pass]; y<png_head.Hoehe;y+=row_increment[pass]) {
      x2 = 0;
      for (int x = starting_col[pass];x<png_head.Breite;x+=col_increment[pass]) {
        switch (png_head.Bit) {
          case 1:
            pix_wert = temp_bild[y2*zeile2+1+x2/8];
            pix_wert = (pix_wert >> (7-x2%8)) & 1;
            str[y*zeile+1+x/8] += (pix_wert << (7-x%8));
          break;
          case 2:
            pix_wert = temp_bild[y2*zeile2+1+x2/4];
            pix_wert = (pix_wert >> (6-(x2%4)*2)) & 3;
            str[y*zeile+1+x/4] += (pix_wert << (6-(x%4)*2));
          break;
          case 4:
            pix_wert = temp_bild[y2*zeile2+1+x2/2];
            pix_wert = (pix_wert >> (4-(x2%2)*4)) & 15;
            str[y*zeile+1+x/2] += (pix_wert << (4-(x%2)*4));
          break;
          default:
            for (int i=0;i<pixel;i++) str[y*zeile+x*pixel+1+i] = temp_bild[y2*zeile2+x2*pixel+1+i];
          break;
        }
        x2++;
      }
      y2++;
    }
  }



  delete[] temp;
  delete[] temp_bild;
}

//////////////////////////////////////////////////////////////////////////
//PNG Daten aus Stream auslesen
bool png_lesen(unsigned char* str, C_IMG *Temp, unsigned int groesse) {
  unsigned char *str_org = str;    //Ausgangsposition
  unsigned int block_groesse = 0;  //Groesse des Chunks
  unsigned int crcode = 0;         //CRC-Wert
  PNG_Header png_head;             //PNG-Header
  int bit = 0;                     //Bittiefe
  unsigned char* encoded = NULL;   //kodierter Datenstrom
  unsigned int enc_groesse = 0;    //Groesse des kodierten Datenstroms
  unsigned char* decoded = NULL;   //dekodierter Datenstrom
  unsigned int dec_groesse = 0;    //Groesse des dekodierten Datenstroms
  int zeile = 0;                   //Anzahl Bytes einer Zeile
  int pixel = 1;                   //Anzahl Bytes pro Pixel
  unsigned char pal[256*3];        //Palette;
  for (int i=0;i<256*3;i++) pal[i] = 0;

  if (PNG_HEADER != *(unsigned long long*) str) return false;
  str+=8;

//IHDR/////////////////////////////////////////////////////////////////
  block_groesse = *(unsigned int*) str; str+=4;
  Endian((char*) &block_groesse,4);
  if (PNG_IHDR != *(unsigned int*) str) {
    cout << "Fehler! Header fehlt!\n!";
    return false;
  }
  str+=4;
  png_head = *(PNG_Header*) str; str+=13;

  str+=4;
  Endian((char*) &png_head.Breite,4);
  Endian((char*) &png_head.Hoehe,4);

  if (png_head.Comp!=0) {cout << "Kompressionstyp!\n"; return false;}
  if (png_head.Filter!=0) {cout << "Filter!\n"; return false;}

  switch(png_head.Farbe) {
    case 0: //Graustufen
    case 3: //Palette
      bit = png_head.Bit << 4;
      zeile = (png_head.Breite*png_head.Bit % 8) ? png_head.Breite*png_head.Bit/8 +1 : png_head.Breite*png_head.Bit/8;
      if (png_head.Bit==16) { zeile *= 2; pixel = 2;}
      zeile++;
    break;
    case 2: //Truecolor
      pixel = 3;
      if (png_head.Bit==16) pixel = 6;
      zeile = png_head.Breite*pixel+1;
      bit = BIT_24;
    break;
    case 4: //Graustufen mit Alpha
      pixel = 2;
      if (png_head.Bit==16) pixel = 4;
      zeile = png_head.Breite*pixel+1;
      bit = BIT_32;
    break;
    case 6: //Truecolor + Alpha
      pixel = 4;
      if (png_head.Bit==16) pixel = 8;
      zeile = png_head.Breite*pixel+1;
      bit = BIT_32;
    break;
    default: cout << "Farbtyp = " << png_head.Farbe << ".\n"; break;
  }

  if (bit==0) return false;

  Temp->Init(png_head.Breite, png_head.Hoehe, bit);

  //Graustufenpalette
  int farb_anzahl = 1;
  for (int i=0;i<bit >> 4;i++) farb_anzahl *= 2;

  if (bit <= BIT_8) {
    for (int i=0;i<farb_anzahl;i++) {
      int grau = i*255/(farb_anzahl-1);
      pal[i*3] = 0xff & grau;
      pal[i*3+1] = 0xff & grau;
      pal[i*3+2] = 0xff & grau;
    }
    Temp->Import_Pal((unsigned char*) pal, BIT_24);
  }

  encoded = new unsigned char[groesse];
  decoded = new unsigned char[png_head.Breite*png_head.Hoehe*10];


//Bilddaten auslesen/////////////////////////////////////////////////////
  bool ende = 0;
  unsigned int typ = 0;
  while(!ende&& (unsigned int) (str-str_org)<groesse) {
    block_groesse = *(unsigned int*) str; str+=4;
    Endian((char*) &block_groesse,4);

    typ = *(unsigned int*) str; str+=4;
    switch(typ) {
      case PNG_PLTE:
        //Bildspeicher auslesen
        if (bit <= BIT_8) Temp->Import_Pal(str, BIT_24 | BE);
      break;
      case PNG_IDAT:
        for (unsigned int i=0;i<block_groesse;i++) {
           encoded[enc_groesse+i] = str[i];
        }
        enc_groesse+=block_groesse;
      break;
      case PNG_IEND: ende = 1; break;
    }

    str += block_groesse;
    str+=4;
  }

//Dekodierung////////////////////////////////////////////////
  int ret = inf(encoded, decoded, enc_groesse, dec_groesse);
  if (ret != 0) zerr(ret);

  //Interlace
  if (png_head.Interlace) deinterlace(decoded, png_head, zeile, pixel, dec_groesse);
  //Filter
  else defilter(decoded, png_head.Hoehe, zeile, pixel);

  for (int y=0;y<png_head.Hoehe;y++) {
    if (png_head.Bit==16)
      for (int i=0;i<zeile/2;i++) decoded[y*zeile+i+1] = decoded[y*zeile+i*2+1];

    Temp->Import_Line(y, &decoded[y*zeile+1], bit | BE);
  }

  delete[] encoded;
  delete[] decoded;

  return true;
}

//////////////////////////////////////////////////
//PNG-Datei schreiben
bool png_schreiben(C_IMG *Temp, char* dat_name) {

  ofstream dat_aus;
  ST_IMG bilddaten;
  Temp->ImgData(bilddaten);

  PNG_Header png_head = {bilddaten.breite,bilddaten.hoehe,0,0,0,0,0};
  unsigned int block_groesse = 0; //Groesse des Chunks
  unsigned int crcode = 0; //CRC
  int zeile = 0;           //Bytes pro Zeile
  int pixel = 1;
  unsigned char* chunk;
  unsigned char* ptr;
  unsigned char* decoded;
  unsigned char* encoded;
  unsigned int enc_groesse;

  switch (bilddaten.bit_pixel) {
    case BIT_1:
    case BIT_2:
    case BIT_4:
    case BIT_8:
      png_head.Bit = bilddaten.bit_pixel >> 4;
      png_head.Farbe = 3;
      zeile = (bilddaten.breite*png_head.Bit % 8) ? bilddaten.breite*png_head.Bit/8 +2 : bilddaten.breite*png_head.Bit/8 +1;
    break;
    case BIT_16_555:
    case BIT_16_565:
    case BIT_24:
      png_head.Bit = 8;
      png_head.Farbe = 2;
      zeile = bilddaten.breite*3+1;
      pixel = 3;
    break;
    case BIT_32:
      png_head.Bit = 8;
      png_head.Farbe = 6;
      zeile = bilddaten.breite*4+1;
      pixel = 4;
    break;
  }

  //Bilddaten einlesen
  decoded = new unsigned char[bilddaten.breite*bilddaten.hoehe*6];
  encoded = new unsigned char[bilddaten.breite*bilddaten.hoehe*50];
  for (int i=0;i<bilddaten.breite*bilddaten.hoehe*5;i++) decoded[i] = encoded[i] = 0;


  for (int y=0;y<bilddaten.hoehe;y++) {
    switch (bilddaten.bit_pixel) {
      case BIT_16_555:
      case BIT_16_565:
        Temp->Export_Line(y, &decoded[y*zeile+1], BIT_24);
        for (int i=0;i<bilddaten.breite;i++) {
          if (bilddaten.bit_pixel==BIT_16_555) bit555_24(&decoded[y*zeile+1+i*3]);
          else bit565_24(&decoded[y*zeile+1+i*3]);
          Endian((char*) &decoded[y*zeile+1+i*3],3);
        }
      break;
      default: Temp->Export_Line(y, &decoded[y*zeile+1], bilddaten.bit_pixel | BE); break;
    }
  }

  //Filter
  if (bilddaten.bit_pixel > BIT_8) enfilter(decoded, png_head.Breite, png_head.Hoehe, zeile, pixel);

  //Kodierung
  int ret = def(decoded, encoded, 9, zeile*bilddaten.hoehe, enc_groesse);
  if (ret != 0) {
    zerr(ret);
    delete[] encoded;
    delete[] decoded;
    return false;
  }

  cout << "Schreibe Datei " << dat_name << ".\n";
  dat_aus.open(dat_name, ios::out | ios::binary);
  dat_aus.write("\x89PNG\x0d\x0a\x1a\x0a", 8);

  Endian((char*) &png_head.Breite, 4);
  Endian((char*) &png_head.Hoehe, 4);

  //IHDR
  block_groesse = 0xd000000;
  chunk = new unsigned char[17];
  chunk[0] = 'I';
  chunk[1] = 'H';
  chunk[2] = 'D';
  chunk[3] = 'R';
  ptr = (unsigned char*) &png_head;
  for (int i=0;i<13;i++) chunk[i+4] = ptr[i];
  crcode = crc(chunk, 17);
  Endian((char*) &crcode,4);
  dat_aus.write((char*) &block_groesse, 4);
  dat_aus.write((char*) chunk, 17);
  dat_aus.write((char*) &crcode, 4);
  delete[] chunk;

  //PLTE
  if (bilddaten.bit_pixel <= BIT_8) {
    block_groesse = bilddaten.farb_anzahl*3;
    Endian((char*) &block_groesse,4);
    dat_aus.write((char*) &block_groesse, 4);
    Endian((char*) &block_groesse,4);

    chunk = new unsigned char[block_groesse+4];
    chunk[0] = 'P';
    chunk[1] = 'L';
    chunk[2] = 'T';
    chunk[3] = 'E';
    Temp->Export_Pal(&chunk[4], BIT_24 | BE);
    crcode = crc(chunk, block_groesse+4);
    Endian((char*) &crcode,4);
    dat_aus.write((char*) chunk, block_groesse+4);
    dat_aus.write((char*) &crcode, 4);
    delete[] chunk;
  }

  //IDAT(s)
  chunk = new unsigned char[8196];
  chunk[0] = 'I';
  chunk[1] = 'D';
  chunk[2] = 'A';
  chunk[3] = 'T';

  for (unsigned int i=0;i<enc_groesse;i+=8192) {
    block_groesse = 8192;
    for (unsigned int j=0;j<8192;j++) {
      if ((i+j) >= enc_groesse) {block_groesse = j; break;}
      chunk[j+4] = encoded[i+j];
    }
    Endian((char*) &block_groesse,4);
    dat_aus.write((char*) &block_groesse, 4);
    Endian((char*) &block_groesse,4);
    crcode = crc(chunk, block_groesse+4);
    Endian((char*) &crcode,4);
    dat_aus.write((char*) chunk, block_groesse+4);
    dat_aus.write((char*) &crcode, 4);
  }

  delete[] chunk;

  //IEND
  block_groesse = 0;
  crcode = 0x826042ae;
  dat_aus.write((char*) &block_groesse, 4);
  dat_aus.write("IEND", 4);
  dat_aus.write((char*) &crcode, 4);

  dat_aus.close();

  delete[] encoded;
  delete[] decoded;


  return true;
}*/
