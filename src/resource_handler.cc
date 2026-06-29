#include "resource_handler.hh"

#include "stb_image/stb_image.h"

// Instantiate static variables
std::map<std::string, Texture_2D> Resource_Handler::textures;
std::map<std::string, Shader_Program> Resource_Handler::shader_programs;

Shader_Program Resource_Handler::load_shader(char const* vShaderFile, char const* fShaderFile, char const* gShaderFile, std::string name)
{
    shader_programs[name] = Shader_Program(vShaderFile, fShaderFile);
    return shader_programs[name];
}

Shader_Program& Resource_Handler::get_shader(std::string name) { return shader_programs[name]; }

Texture_2D Resource_Handler::load_texture(char const* file, bool alpha, std::string name)
{
    textures[name] = load_texture_from_file(file, alpha);
    return textures[name];
}

Texture_2D& Resource_Handler::get_texture(std::string name) { return textures[name]; }

void Resource_Handler::clear()
{
    // (properly) delete all shaders
    for (auto iter : shader_programs)
        glDeleteProgram(iter.second.ID);
    // (properly) delete all textures
    for (auto iter : textures)
        glDeleteTextures(1, &iter.second.id);
}

Texture_2D Resource_Handler::load_texture_from_file(char const* file, bool alpha)
{
    // create texture object
    Texture_2D texture;
    if (alpha)
    {
        texture.internal_format = GL_RGBA;
        texture.image_format = GL_RGBA;
    }
    // load image
    int width, height, nrChannels;
    unsigned char* data = stbi_load(file, &width, &height, &nrChannels, 0);
    // now generate texture
    texture.generate(width, height, data);
    // and finally free image data
    stbi_image_free(data);
    return texture;
}
