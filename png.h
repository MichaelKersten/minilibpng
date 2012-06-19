
#ifndef MK_PNG
#define MK_PNG

#include <zlib.h>

//Endian conversion
inline void endian(unsigned int *val);

//filter method 4 paeth predictor
inline unsigned char paeth_predictor(int a, int b, int c);

//////////////////////////////////////////////////////////////////////////
//png file Class
class PngFile {
  protected:
    //png header info
    unsigned char bit;       // bit size
    unsigned char color;     // color type
    unsigned char comp;      // compression method
    unsigned char filter;    // filter nethod
    unsigned char interlace; // interlacing on/off

    unsigned int line_len;    //length of line
    unsigned char pixel_size; //size of pixel for filter algorithm
    unsigned int interlace_count;
    int last_pass;            // number of the last pass (interlace format only)

    unsigned char *file;      //png file pointer
    unsigned char *file_ptr;  //temp. pointer

    z_stream strm;            //zlib stream


    unsigned int pal[256];    //palette
    int pal_number;           //number of palette entries

  public:
    unsigned int width;
    unsigned int height;
    unsigned int background;  // background color
    unsigned int length;      // real-length in bytes of the PNG file
    unsigned int scratch_size;// size of the scratch area in bytes
    unsigned int last_row;    // number of the last row delivered via read

  //constructor
  PngFile(void *input_data, unsigned int input_length);
  //destructor
  ~PngFile();

  private:
  //get next IDAT block
  int next_block();

  //decompress line
  int dec_line(unsigned char *line, unsigned int count);

  //read line interlaced
  int read_interlaced(void *row, bool use_bgrx, void *scratch);

  public:
  //read line
  int read(void *row, bool use_bgrx, void *scratch);
};










/*


#include "img.h"

#define PNG_HEADER 0x0a1a0a0d474e5089
#define PNG_IHDR 0x52444849
#define PNG_IDAT 0x54414449
#define PNG_PLTE 0x45544c50
#define PNG_IEND 0x444e4549


//////////////////////////////////////////////////////////////////////////
//Filter Methode 4 Paeth Predictor
unsigned char paeth_predictor(int a, int b, int c);

//////////////////////////////////////////////////////////////////////////
//Scanline zusammenrechnen
int differenzen(unsigned char* str, int anzahl);

//////////////////////////////////////////////////////////////////////////
//PNG Daten filtern
void enfilter(unsigned char* str, int breite, int hoehe, int zeile, int pixel);

//////////////////////////////////////////////////////////////////////////
//PNG Daten entfiltern
void defilter(unsigned char* str, int hoehe, int zeile, int pixel);

//////////////////////////////////////////////////////////////////////////
//PNG Daten deinterlacen
void deinterlace(unsigned char* str, PNG_Header png_head, int zeile, int pixel, int str_groesse);

//////////////////////////////////////////////////////////////////////////
//PNG Daten aus Stream auslesen
bool png_lesen(unsigned char* str, C_IMG *Temp, unsigned int groesse);

//////////////////////////////////////////////////
//PNG-Datei schreiben
bool png_schreiben(C_IMG *Temp, char* dat_name);
*/
#endif
