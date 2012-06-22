#include <iostream>
#include <fstream>
#include "png.h"

using namespace std;

struct testfile {
  char name[20];
  unsigned int size;
};


void tfcpy(testfile *file, char* name, unsigned int size) {
  strcpy(file->name,name);
  file->size = size;
}

void create_file_name(char* filename, char* name, char* type) {
  strcpy(filename,name);
  unsigned int pos = strlen(filename);
  strcpy(&filename[pos],type);
}

int main(int argc, char *argv[]) {
  char name[20];

  //test images
  testfile files[30];

  //grayscale (color type 0)
  tfcpy(&files[0],"img0_1",      591); //1-Bit
  tfcpy(&files[1],"img0_1_i",    861); //1-Bit interlaced
  tfcpy(&files[2],"img0_2",     1241); //2-Bit
  tfcpy(&files[3],"img0_2_i",   1691); //2-Bit interlaced
  tfcpy(&files[4],"img0_4",     3262); //4-Bit
  tfcpy(&files[5],"img0_4_i",   4232); //4-Bit interlaced
  tfcpy(&files[6],"img0_8",     8830); //8-Bit
  tfcpy(&files[7],"img0_8_i",  10942); //8-Bit interlaced
  tfcpy(&files[8],"img0_16",   11161); //16-Bit
  tfcpy(&files[9],"img0_16_i", 16409); //16-Bit interlaced

  //truecolor (color type 2)
  tfcpy(&files[10],"img2_8",    21908); //8-Bit
  tfcpy(&files[11],"img2_8_i",  27885); //8-Bit interlaced
  tfcpy(&files[12],"img2_16",   84337); //16-Bit
  tfcpy(&files[13],"img2_16_i", 38269); //16-Bit interlaced

  //palette type (color type 3)
  tfcpy(&files[14],"img3_1",    1013); //1-Bit
  tfcpy(&files[15],"img3_1_i",  1278); //1-Bit interlaced
  tfcpy(&files[16],"img3_2",    1702); //2-Bit
  tfcpy(&files[17],"img3_2_i",  2152); //2-Bit interlaced
  tfcpy(&files[18],"img3_4",    3874); //4-Bit
  tfcpy(&files[19],"img3_4_i",  4879); //4-Bit interlaced
  tfcpy(&files[20],"img3_8",    8306); //8-Bit
  tfcpy(&files[21],"img3_8_i", 10613); //8-Bit interlaced

  //grayscale with alpha (color type 4)
  tfcpy(&files[22],"img4_8",    2546); //8-Bit
  tfcpy(&files[23],"img4_8_i",  3546); //8-Bit interlaced
  tfcpy(&files[24],"img4_16",   7311); //16-Bit
  tfcpy(&files[25],"img4_16_i", 4909); //16-Bit interlaced

  //truecolor with alpha (color type 6)
  tfcpy(&files[26],"img6_8",    3826); //8-Bit
  tfcpy(&files[27],"img6_8_i",  5316); //8-Bit interlaced
  tfcpy(&files[28],"img6_16",  10153); //16-Bit
  tfcpy(&files[29],"img6_16_i", 6976); //16-Bit interlaced

  unsigned int complete_size = 310018;

  unsigned char *data = NULL;
  unsigned char *data_ptr = NULL;

  ifstream file_in;
  ofstream file_out;


  data = new unsigned char[complete_size];
  data_ptr = data;


  cout << "Read Files.\n===========\n";

  for (int i=0;i<30;i++) {
    create_file_name(name, files[i].name, ".png");

    cout << "Read File " << name << " -> ";

    if (!file_in) {cout << "ERROR!\n";}
    else {
      file_in.open(name, ios::in | ios::binary);
      file_in.read((char*) data_ptr,files[i].size);
      file_in.close();
      cout << "OK\n";
    }
    data_ptr += files[i].size;
  }

  data_ptr = data;

  cout << "\nCreate PngFile objects.\n=======================\n";

  PngFile *PNG[30];

  for (int i=0;i<30;i++) {
    cout << files[i].name << ".png -> ";
    PNG[i] = new PngFile(data_ptr, files[i].size);
    if (PNG[i]->length) cout << "OK\n";
    else cout << "bad file!!\n";
    data_ptr+=files[i].size;
  }


  cout << "\nRead image data and write TGA files.\n====================================\n";

  cout << "24Bit:\n------\n";

  for (int i=0;i<30;i++) {
    create_file_name(name, files[i].name, "_24.tga");
    if (PNG[i]->length) {
      cout << files[i].name << ".png -> ";
      unsigned char *image;
      unsigned char *scratch;
      unsigned int image_size = PNG[i]->width*PNG[i]->height*3;
      image = new unsigned char[image_size];
      scratch = new unsigned char[PNG[i]->scratch_size];
      int ret = 0;
      while (!ret) {
        ret = PNG[i]->read(&image[PNG[i]->last_row*PNG[i]->width*3],0,scratch);
      }

      cout << "Write File " << name << " -> ";
      file_out.open(name, ios::out | ios::binary);
      if (!file_out) {cout << "ERROR!\n";}
      else {
        file_out.write("\x00\x00\x02\x00\x00\x00\x00\x00\x00\x00\x00\x00",12);
        file_out.write((char*) &PNG[i]->width,2);
        file_out.write((char*) &PNG[i]->height,2);
        file_out.write("\x18\x20",2);
        file_out.write((char*) image, image_size);
        file_out.close();
        cout << "OK\n";
      }

      delete[] image;
      delete[] scratch;
    }
  }


  cout << "\n32Bit:\n------\n";

  for (int i=0;i<30;i++) {
    create_file_name(name, files[i].name, "_32.tga");
    if (PNG[i]->length) {
      cout << files[i].name << ".png -> ";
      unsigned char *image;
      unsigned char *scratch;
      unsigned int image_size = PNG[i]->width*PNG[i]->height*4;
      image = new unsigned char[image_size];
      scratch = new unsigned char[PNG[i]->scratch_size];
      int ret = 0;
      while (!ret) {
        ret = PNG[i]->read(&image[PNG[i]->last_row*PNG[i]->width*4],1,scratch);
      }

      cout << "Write File " << name << " -> ";
      file_out.open(name, ios::out | ios::binary);
      if (!file_out) {cout << "ERROR!\n";}
      else {
        file_out.write("\x00\x00\x02\x00\x00\x00\x00\x00\x00\x00\x00\x00",12);
        file_out.write((char*) &PNG[i]->width,2);
        file_out.write((char*) &PNG[i]->height,2);
        file_out.write("\x20\x20",2);
        file_out.write((char*) image, image_size);
        file_out.close();
        cout << "OK\n";
      }

      delete[] image;
      delete[] scratch;
    }
  }

  //cleanup
  for (int i=0;i<30;i++) {
    delete PNG[i];
  }
  delete[] data;


  return 0;
}
