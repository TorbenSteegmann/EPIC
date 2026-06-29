#include "shader_program.hh"

#include <glad/glad.h>
#include <fstream>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <sstream>

Shader_Program::Shader_Program(char const* vertex_path, char const* fragment_path)
{
    std::string vertex_code = read_file(vertex_path);
    std::string fragment_code = read_file(fragment_path);
    char const* vertex_source = vertex_code.c_str();
    char const* fragment_source = fragment_code.c_str();

    unsigned int vertex_shader, fragment_shader;

    // Create Shaders
    vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    ID = glCreateProgram();

    // Load Shader Source Code
    glShaderSource(vertex_shader, 1, &vertex_source, NULL);
    glShaderSource(fragment_shader, 1, &fragment_source, NULL);

    // Compile Shaders
    glCompileShader(vertex_shader);
    checkCompileErrors(vertex_shader, "VERTEX");
    glCompileShader(fragment_shader);
    checkCompileErrors(fragment_shader, "FRAGMENT");

    // Attach and Link Shaders
    glAttachShader(ID, vertex_shader);
    glAttachShader(ID, fragment_shader);
    glLinkProgram(ID);
    checkCompileErrors(ID, "PROGRAM");

    // Delete the shaders as they're linked into our program now and no longer necessary
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
}

std::string Shader_Program::read_file(char const* path)
{
    // 1. retrieve the vertex/fragment source code from file path
    std::string code;
    try
    {
        // open files
        std::ifstream file(path);
        std::stringstream input_stream;
        // read file's buffer contents into streams
        input_stream << file.rdbuf();
        // close file handlers
        file.close();
        // convert stream into string
        code = input_stream.str();
    }
    catch (std::exception e)
    {
        std::cout << "ERROR::SHADER: Failed to read shader files" << std::endl;
    }
    return code;
}

Shader_Program& Shader_Program::use()
{
    glUseProgram(this->ID);
    return *this;
}

void Shader_Program::set_float(char const* name, float value)
{
    this->use();
    glUniform1f(glGetUniformLocation(this->ID, name), value);
}
void Shader_Program::set_integer(char const* name, int value)
{
    this->use();
    glUniform1i(glGetUniformLocation(this->ID, name), value);
}
void Shader_Program::set_vector_2f(char const* name, float x, float y)
{
    this->use();
    glUniform2f(glGetUniformLocation(this->ID, name), x, y);
}
void Shader_Program::set_vector_2f(char const* name, glm::vec2 const& value)
{
    this->use();
    glUniform2f(glGetUniformLocation(this->ID, name), value.x, value.y);
}
void Shader_Program::set_vector_3f(char const* name, float x, float y, float z)
{
    this->use();
    glUniform3f(glGetUniformLocation(this->ID, name), x, y, z);
}
void Shader_Program::set_vector_3f(char const* name, glm::vec3 const& value)
{
    this->use();
    glUniform3f(glGetUniformLocation(this->ID, name), value.x, value.y, value.z);
}
void Shader_Program::set_vector_4f(char const* name, float x, float y, float z, float w)
{
    this->use();
    glUniform4f(glGetUniformLocation(this->ID, name), x, y, z, w);
}
void Shader_Program::set_vector_4f(char const* name, glm::vec4 const& value)
{
    this->use();
    glUniform4f(glGetUniformLocation(this->ID, name), value.x, value.y, value.z, value.w);
}
void Shader_Program::set_matrix_4(char const* name, glm::mat4 const& matrix)
{
    this->use();
    glUniformMatrix4fv(glGetUniformLocation(this->ID, name), 1, false, glm::value_ptr(matrix));
}

void Shader_Program::checkCompileErrors(unsigned int object, std::string type)
{
    int success;
    char infoLog[1024];
    if (type != "PROGRAM")
    {
        glGetShaderiv(object, GL_COMPILE_STATUS, &success);
        if (!success)
        {
            glGetShaderInfoLog(object, 1024, NULL, infoLog);
            std::cout << "| ERROR::SHADER: Compile-time error: Type: " << type << "\n"
                      << infoLog << "\n --------------------------------------------------------- " << std::endl;
        }
    }
    else
    {
        glGetProgramiv(object, GL_LINK_STATUS, &success);
        if (!success)
        {
            glGetProgramInfoLog(object, 1024, NULL, infoLog);
            std::cout << "| ERROR::Shader: Link-time error: Type: " << type << "\n"
                      << infoLog << "\n --------------------------------------------------------- " << std::endl;
        }
    }
}
