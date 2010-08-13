#include <math.h>

inline float Vector3::length()
{
	return sqrt(x * x + y * y + z * z);
}

inline float Vector3::lengthsq()
{
	return x * x + y * y + z * z;
}

inline Vector3 operator +(const Vector3 &a, const Vector3 &b)
{
	return Vector3(a.x + b.x, a.y + b.y, a.z + b.z);
}

inline Vector3 operator -(const Vector3 &a, const Vector3 &b)
{
	return Vector3(a.x - b.x, a.y - b.y, a.z - b.z);
}

inline Vector3 operator *(const Vector3 &a, const Vector3 &b)
{
	return Vector3(a.x * b.x, a.y * b.y, a.z * b.z);
}

inline Vector3 operator /(const Vector3 &a, const Vector3 &b)
{
	return Vector3(a.x / b.x, a.y / b.y, a.z / b.z);
}


inline Vector3 operator -(const Vector3 &vec)
{
	return Vector3(-vec.x, -vec.y, -vec.z);
}

inline Vector3 operator *(const Vector3 &vec, float s)
{
	return Vector3(vec.x * s, vec.y * s, vec.z * s);
}


inline float dot(const Vector3 &a, const Vector3 &b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vector3 cross(const Vector3 &a, const Vector3 &b)
{
	return Vector3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
