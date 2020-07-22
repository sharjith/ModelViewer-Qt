#include "Cube.h"
#include <cstdio>

Cube::Cube(QOpenGLShaderProgram *prog, GLfloat size) : QuadMesh(prog, "Cube")
{
    GLfloat side = size / 2.0f;

    std::vector<GLfloat> p = {
        // Front
        -side, -side, side, side, -side, side, side, side, side, -side, side, side,
        // Right
        side, -side, side, side, -side, -side, side, side, -side, side, side, side,
        // Back
        -side, -side, -side, -side, side, -side, side, side, -side, side, -side, -side,
        // Left
        -side, -side, side, -side, side, side, -side, side, -side, -side, -side, -side,
        // Bottom
        -side, -side, side, -side, -side, -side, side, -side, -side, side, -side, side,
        // Top
        -side, side, side, side, side, side, side, side, -side, -side, side, -side};

    std::vector<GLfloat> n = {
        // Front
        0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
        // Right
        1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
        // Back
        0.0f, 0.0f, -1.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, -1.0f,
        // Left
        -1.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f,
        // Bottom
        0.0f, -1.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, -1.0f, 0.0f,
        // Top
        0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f};

    std::vector<GLfloat> tex = {
        // Front
        0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
        // Right
        0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
        // Back
        0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
        // Left
        0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
        // Bottom
        0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
        // Top
        0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};

    /*std::vector<GLuint> el = {
        0,1,2,0,2,3,
        4,5,6,4,6,7,
        8,9,10,8,10,11,
        12,13,14,12,14,15,
        16,17,18,16,18,19,
        20,21,22,20,22,23
    };*/
    std::vector<GLuint> el = {
        0, 1, 2, 3,
        4, 5, 6, 7,
        8, 9, 10, 11,
        12, 13, 14, 15,
        16, 17, 18, 19,
        20, 21, 22, 23};

    initBuffers(&el, &p, &n, &tex);

    _boundingSphere.setCenter(0, 0, 0);
    _boundingSphere.setRadius(sqrt(3) * (size / 2));
}
