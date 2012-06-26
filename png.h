
#ifndef MK_PNG
#define MK_PNG

#include <zlib.h>

//filter method 4 paeth predictor
inline unsigned char paeth_predictor(int a, int b, int c);


//////////////////////////////////////////////////////////////////////////
//png file Class
class PngFile {
  protected:
    //png header info
    unsigned char bit;            // bit size
    unsigned char color;          // color type
    unsigned char interlace;      // interlacing on/off

    unsigned char channel;        //number of channels in a single pixel
    unsigned int interlace_count;
    int last_pass;                // number of the last pass

    unsigned char *file;          //png file pointer
    unsigned char *file_ptr;      //temp. pointer

    z_stream strm;                //zlib stream

    unsigned char pal[256][3];    //palette

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

  unsigned int read_dword(bool be=true);

  void reset();

  public:
  //read line
  int read(void *row, bool use_bgrx, void *scratch);
};


#endif
