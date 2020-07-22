#include "Point.h"
#include <glm/glm.hpp>


Point::Point(const float& x, const float& y, const float& z)
{
	_x = x;
	_y = y;
	_z = z;
}


Point::~Point()
{
}

float Point::distance(const Point& other)
{
	return sqrt(pow((other._x - _x), 2) + pow((other._y - _y), 2) + pow((other._z - _z), 2));
}