#include "vector.h"

Vector2::Vector2() : x(0), y(0) {}

Vector2::Vector2(float x, float y)
{
	this->x = x;
	this->y = y;
}


Vector3::Vector3() : x(0), y(0), z(0) {}

Vector3::Vector3(float x, float y, float z)
{
	this->x = x;
	this->y = y;
	this->z = z;
}

void Vector3::normalize()
{
	float len = sqrt(x * x + y * y + z * z);
	if(len != 0.0) {
		x /= len;
		y /= len;
		z /= len;
	}
}
