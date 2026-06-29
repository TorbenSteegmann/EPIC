#pragma once

#include <map>
#include <string>

#include <glad/glad.h>

#include "shader_program.hh"
#include "texture_2d.hh"

// A static singleton ResourceManager class that hosts several
// functions to load Textures and Shaders. Each loaded texture
// and/or shader is also stored for future reference by string
// handles. All functions and resources are static and no
// public constructor is defined.
class Resource_Handler
{
public:
    // resource storage
    static std::map<std::string, Shader_Program> shader_programs;
    static std::map<std::string, Texture_2D> textures;
    // loads (and generates) a shader program from file loading vertex, fragment (and geometry) shader's source code. If gShaderFile is not nullptr, it also loads a geometry shader
    static Shader_Program load_shader(char const* vShaderFile, char const* fShaderFile, char const* gShaderFile, std::string name);
    // retrieves a stored sader
    static Shader_Program& get_shader(std::string name);
    // loads (and generates) a texture from file
    static Texture_2D load_texture(char const* file, bool alpha, std::string name);
    // retrieves a stored texture
    static Texture_2D& get_texture(std::string name);
    // properly de-allocates all loaded resources
    static void clear();

private:
    // private constructor, that is we do not want any actual resource manager objects. Its members and functions should be publicly available (static).
    Resource_Handler() {}
    // loads a single texture from file
    static Texture_2D load_texture_from_file(char const* file, bool alpha);
};
