#ifndef VECTOR_H_
#define VECTOR_H_

class Vector2 {
public:
	float x, y;

	Vector2();
	Vector2(float x, float y);
};

class Vector3 {
public:
	float x, y, z;

	Vector3();
	Vector3(float x, float y, float z);
	Vector3(const float *arr);

	void normalize();
	inline float length();
	inline float lengthsq();
};

inline Vector3 operator +(const Vector3 &a, const Vector3 &b);
inline Vector3 operator -(const Vector3 &a, const Vector3 &b);
inline Vector3 operator *(const Vector3 &a, const Vector3 &b);
inline Vector3 operator /(const Vector3 &a, const Vector3 &b);

inline Vector3 operator -(const Vector3 &vec);
inline Vector3 operator *(const Vector3 &vec, float s);
inline Vector3 operator /(const Vector3 &vec, float s);

inline float dot(const Vector3 &a, const Vector3 &b);
inline Vector3 cross(const Vector3 &a, const Vector3 &b);

inline Vector3 reflect(const Vector3 &v, const Vector3 &n);

#include "vector.inl"

#endif	/* VECTOR_H_ */
