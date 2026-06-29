#include "texture_2d.hh"

Texture_2D::Texture_2D()
  : width(0), height(0), internal_format(GL_RGB), image_format(GL_RGB), wrap_S(GL_REPEAT), wrap_T(GL_REPEAT), filter_min(GL_LINEAR_MIPMAP_LINEAR), filter_max(GL_LINEAR)
{
    glGenTextures(1, &this->id);
};

void Texture_2D::generate(unsigned int width, unsigned int height, unsigned char* data)
{
    this->width = width;
    this->height = height;
    // create Texture
    glBindTexture(GL_TEXTURE_2D, this->id);
    glTexImage2D(GL_TEXTURE_2D, 0, this->internal_format, width, height, 0, this->image_format, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    // set Texture wrap and filter modes
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, this->wrap_S);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, this->wrap_T);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, this->filter_min);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, this->filter_max);
    // unbind texture
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Texture_2D::bind() const { glBindTexture(GL_TEXTURE_2D, this->id); }
