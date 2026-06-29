#pragma once

#include <glm/glm.hpp>
#include <string>

class Shader_Program
{
public:
    // state
    unsigned int ID;
    // constructor
    Shader_Program(){};
    Shader_Program(char const* vertex_source, char const* fragment_source);
    std::string read_file(char const* path);
    // sets the current shader as active
    Shader_Program& use();
    // utility functions
    void set_float(char const* name, float value);
    void set_integer(char const* name, int value);
    void set_vector_2f(char const* name, float x, float y);
    void set_vector_2f(char const* name, glm::vec2 const& value);
    void set_vector_3f(char const* name, float x, float y, float z);
    void set_vector_3f(char const* name, glm::vec3 const& value);
    void set_vector_4f(char const* name, float x, float y, float z, float w);
    void set_vector_4f(char const* name, glm::vec4 const& value);
    void set_matrix_4(char const* name, glm::mat4 const& matrix);

private:
    // checks if compilation or linking failed and if so, print the error logs
    void checkCompileErrors(unsigned int object, std::string type);
};
