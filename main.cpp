#include <iostream>
#include <fstream>
#include "png.h"

using namespace std;

int main(int argc, char *argv[]) {
  PngFile *PNG;
  unsigned char* data;
  unsigned int size;
  unsigned int image_size;
  ifstream dat_ein;
  ofstream dat_aus;

  unsigned char *image;
  unsigned char *scratch;



  int typ = 3;
  int bit = typ*8;




  dat_ein.open("bild24.png", ios::in | ios::binary);
  dat_ein.seekg(0,ios::end);
  size = dat_ein.tellg();
  dat_ein.seekg(0);
  data = new unsigned char[size];
  dat_ein.read((char*) data,size);
  dat_ein.close();

  PNG = new PngFile(data, size);
  image_size = PNG->width*PNG->height*typ;
  image = new unsigned char[image_size];
  scratch = new unsigned char[PNG->scratch_size];

  while ( 0 == PNG->read(&image[PNG->last_row*PNG->width*typ],(typ==4),scratch) );


  dat_aus.open("test.tga", ios::out | ios::binary);
  dat_aus.write("\x00\x00\x02\x00\x00\x00\x00\x00\x00\x00\x00\x00",12);
  dat_aus.write((char*) &PNG->width,2);
  dat_aus.write((char*) &PNG->height,2);
  dat_aus.write((char*) &bit,1);
  dat_aus.write("\x20",1);
  dat_aus.write((char*) image, image_size);
  dat_aus.close();



  delete[] image;
  delete[] scratch;
  delete[] data;
  delete PNG;


  return 0;
}
