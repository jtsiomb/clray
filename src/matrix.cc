#include <string.h>
#include "matrix.h"

#define M(x, y)	((y) * 4 + (x))

Matrix4x4::Matrix4x4()
{
	memset(m, 0, sizeof m);
	m[0] = m[5] = m[10] = m[15] = 1.0;
}

Matrix4x4::Matrix4x4(const float *mat)
{
	memcpy(m, mat, sizeof m);
}

Matrix4x4::Matrix4x4(float m00, float m01, float m02, float m03,
		float m10, float m11, float m12, float m13,
		float m20, float m21, float m22, float m23,
		float m30, float m31, float m32, float m33)
{
	m[M(0, 0)] = m00; m[M(0, 1)] = m01; m[M(0, 2)] = m02; m[M(0, 3)] = m03;
	m[M(1, 0)] = m10; m[M(1, 1)] = m11; m[M(1, 2)] = m12; m[M(1, 3)] = m13;
	m[M(2, 0)] = m20; m[M(2, 1)] = m21; m[M(2, 2)] = m22; m[M(2, 3)] = m23;
	m[M(3, 0)] = m30; m[M(3, 1)] = m31; m[M(3, 2)] = m32; m[M(3, 3)] = m33;
}

Matrix4x4::Matrix4x4(const Matrix4x4 &mat)
{
	memcpy(m, mat.m, sizeof m);
}

Matrix4x4 &Matrix4x4::operator =(const Matrix4x4 &mat)
{
	memcpy(m, mat.m, sizeof m);
	return *this;
}

void Matrix4x4::identity()
{
	memset(m, 0, sizeof m);
	m[0] = m[5] = m[10] = m[15] = 1.0;
}

float Matrix4x4::determinant() const
{
	float det11 =	(m[M(1, 1)] * (m[M(2, 2)] * m[M(3, 3)] - m[M(3, 2)] * m[M(2, 3)])) -
					(m[M(1, 2)] * (m[M(2, 1)] * m[M(3, 3)] - m[M(3, 1)] * m[M(2, 3)])) +
					(m[M(1, 3)] * (m[M(2, 1)] * m[M(3, 2)] - m[M(3, 1)] * m[M(2, 2)]));

	float det12 =	(m[M(1, 0)] * (m[M(2, 2)] * m[M(3, 3)] - m[M(3, 2)] * m[M(2, 3)])) -
					(m[M(1, 2)] * (m[M(2, 0)] * m[M(3, 3)] - m[M(3, 0)] * m[M(2, 3)])) +
					(m[M(1, 3)] * (m[M(2, 0)] * m[M(3, 2)] - m[M(3, 0)] * m[M(2, 2)]));

	float det13 =	(m[M(1, 0)] * (m[M(2, 1)] * m[M(3, 3)] - m[M(3, 1)] * m[M(2, 3)])) -
					(m[M(1, 1)] * (m[M(2, 0)] * m[M(3, 3)] - m[M(3, 0)] * m[M(2, 3)])) +
					(m[M(1, 3)] * (m[M(2, 0)] * m[M(3, 1)] - m[M(3, 0)] * m[M(2, 1)]));

	float det14 =	(m[M(1, 0)] * (m[M(2, 1)] * m[M(3, 2)] - m[M(3, 1)] * m[M(2, 2)])) -
					(m[M(1, 1)] * (m[M(2, 0)] * m[M(3, 2)] - m[M(3, 0)] * m[M(2, 2)])) +
					(m[M(1, 2)] * (m[M(2, 0)] * m[M(3, 1)] - m[M(3, 0)] * m[M(2, 1)]));

	return m[M(0, 0)] * det11 - m[M(0, 1)] * det12 + m[M(0, 2)] * det13 - m[M(0, 3)] * det14;
}

Matrix4x4 Matrix4x4::adjoint() const
{
	Matrix4x4 coef;

	coef.m[M(0, 0)] =	(m[M(1, 1)] * (m[M(2, 2)] * m[M(3, 3)] - m[M(3, 2)] * m[M(2, 3)])) -
						(m[M(1, 2)] * (m[M(2, 1)] * m[M(3, 3)] - m[M(3, 1)] * m[M(2, 3)])) +
						(m[M(1, 3)] * (m[M(2, 1)] * m[M(3, 2)] - m[M(3, 1)] * m[M(2, 2)]));
	coef.m[M(0, 1)] =	(m[M(1, 0)] * (m[M(2, 2)] * m[M(3, 3)] - m[M(3, 2)] * m[M(2, 3)])) -
						(m[M(1, 2)] * (m[M(2, 0)] * m[M(3, 3)] - m[M(3, 0)] * m[M(2, 3)])) +
						(m[M(1, 3)] * (m[M(2, 0)] * m[M(3, 2)] - m[M(3, 0)] * m[M(2, 2)]));
	coef.m[M(0, 2)] =	(m[M(1, 0)] * (m[M(2, 1)] * m[M(3, 3)] - m[M(3, 1)] * m[M(2, 3)])) -
						(m[M(1, 1)] * (m[M(2, 0)] * m[M(3, 3)] - m[M(3, 0)] * m[M(2, 3)])) +
						(m[M(1, 3)] * (m[M(2, 0)] * m[M(3, 1)] - m[M(3, 0)] * m[M(2, 1)]));
	coef.m[M(0, 3)] =	(m[M(1, 0)] * (m[M(2, 1)] * m[M(3, 2)] - m[M(3, 1)] * m[M(2, 2)])) -
						(m[M(1, 1)] * (m[M(2, 0)] * m[M(3, 2)] - m[M(3, 0)] * m[M(2, 2)])) +
						(m[M(1, 2)] * (m[M(2, 0)] * m[M(3, 1)] - m[M(3, 0)] * m[M(2, 1)]));

	coef.m[M(1, 0)] =	(m[M(0, 1)] * (m[M(2, 2)] * m[M(3, 3)] - m[M(3, 2)] * m[M(2, 3)])) -
						(m[M(0, 2)] * (m[M(2, 1)] * m[M(3, 3)] - m[M(3, 1)] * m[M(2, 3)])) +
						(m[M(0, 3)] * (m[M(2, 1)] * m[M(3, 2)] - m[M(3, 1)] * m[M(2, 2)]));
	coef.m[M(1, 1)] =	(m[M(0, 0)] * (m[M(2, 2)] * m[M(3, 3)] - m[M(3, 2)] * m[M(2, 3)])) -
						(m[M(0, 2)] * (m[M(2, 0)] * m[M(3, 3)] - m[M(3, 0)] * m[M(2, 3)])) +
						(m[M(0, 3)] * (m[M(2, 0)] * m[M(3, 2)] - m[M(3, 0)] * m[M(2, 2)]));
	coef.m[M(1, 2)] =	(m[M(0, 0)] * (m[M(2, 1)] * m[M(3, 3)] - m[M(3, 1)] * m[M(2, 3)])) -
						(m[M(0, 1)] * (m[M(2, 0)] * m[M(3, 3)] - m[M(3, 0)] * m[M(2, 3)])) +
						(m[M(0, 3)] * (m[M(2, 0)] * m[M(3, 1)] - m[M(3, 0)] * m[M(2, 1)]));
	coef.m[M(1, 3)] =	(m[M(0, 0)] * (m[M(2, 1)] * m[M(3, 2)] - m[M(3, 1)] * m[M(2, 2)])) -
						(m[M(0, 1)] * (m[M(2, 0)] * m[M(3, 2)] - m[M(3, 0)] * m[M(2, 2)])) +
						(m[M(0, 2)] * (m[M(2, 0)] * m[M(3, 1)] - m[M(3, 0)] * m[M(2, 1)]));

	coef.m[M(2, 0)] =	(m[M(0, 1)] * (m[M(1, 2)] * m[M(3, 3)] - m[M(3, 2)] * m[M(1, 3)])) -
						(m[M(0, 2)] * (m[M(1, 1)] * m[M(3, 3)] - m[M(3, 1)] * m[M(1, 3)])) +
						(m[M(0, 3)] * (m[M(1, 1)] * m[M(3, 2)] - m[M(3, 1)] * m[M(1, 2)]));
	coef.m[M(2, 1)] =	(m[M(0, 0)] * (m[M(1, 2)] * m[M(3, 3)] - m[M(3, 2)] * m[M(1, 3)])) -
						(m[M(0, 2)] * (m[M(1, 0)] * m[M(3, 3)] - m[M(3, 0)] * m[M(1, 3)])) +
						(m[M(0, 3)] * (m[M(1, 0)] * m[M(3, 2)] - m[M(3, 0)] * m[M(1, 2)]));
	coef.m[M(2, 2)] =	(m[M(0, 0)] * (m[M(1, 1)] * m[M(3, 3)] - m[M(3, 1)] * m[M(1, 3)])) -
						(m[M(0, 1)] * (m[M(1, 0)] * m[M(3, 3)] - m[M(3, 0)] * m[M(1, 3)])) +
						(m[M(0, 3)] * (m[M(1, 0)] * m[M(3, 1)] - m[M(3, 0)] * m[M(1, 1)]));
	coef.m[M(2, 3)] =	(m[M(0, 0)] * (m[M(1, 1)] * m[M(3, 2)] - m[M(3, 1)] * m[M(1, 2)])) -
						(m[M(0, 1)] * (m[M(1, 0)] * m[M(3, 2)] - m[M(3, 0)] * m[M(1, 2)])) +
						(m[M(0, 2)] * (m[M(1, 0)] * m[M(3, 1)] - m[M(3, 0)] * m[M(1, 1)]));

	coef.m[M(3, 0)] =	(m[M(0, 1)] * (m[M(1, 2)] * m[M(2, 3)] - m[M(2, 2)] * m[M(1, 3)])) -
						(m[M(0, 2)] * (m[M(1, 1)] * m[M(2, 3)] - m[M(2, 1)] * m[M(1, 3)])) +
						(m[M(0, 3)] * (m[M(1, 1)] * m[M(2, 2)] - m[M(2, 1)] * m[M(1, 2)]));
	coef.m[M(3, 1)] =	(m[M(0, 0)] * (m[M(1, 2)] * m[M(2, 3)] - m[M(2, 2)] * m[M(1, 3)])) -
						(m[M(0, 2)] * (m[M(1, 0)] * m[M(2, 3)] - m[M(2, 0)] * m[M(1, 3)])) +
						(m[M(0, 3)] * (m[M(1, 0)] * m[M(2, 2)] - m[M(2, 0)] * m[M(1, 2)]));
	coef.m[M(3, 2)] =	(m[M(0, 0)] * (m[M(1, 1)] * m[M(2, 3)] - m[M(2, 1)] * m[M(1, 3)])) -
						(m[M(0, 1)] * (m[M(1, 0)] * m[M(2, 3)] - m[M(2, 0)] * m[M(1, 3)])) +
						(m[M(0, 3)] * (m[M(1, 0)] * m[M(2, 1)] - m[M(2, 0)] * m[M(1, 1)]));
	coef.m[M(3, 3)] =	(m[M(0, 0)] * (m[M(1, 1)] * m[M(2, 2)] - m[M(2, 1)] * m[M(1, 2)])) -
						(m[M(0, 1)] * (m[M(1, 0)] * m[M(2, 2)] - m[M(2, 0)] * m[M(1, 2)])) +
						(m[M(0, 2)] * (m[M(1, 0)] * m[M(2, 1)] - m[M(2, 0)] * m[M(1, 1)]));

	coef.transpose();

	for(int i=0; i<4; i++) {
		for(int j=0; j<4; j++) {
			coef.m[M(i, j)] = j % 2 ? -coef.m[M(i, j)] : coef.m[M(i, j)];
			if(i % 2) coef.m[M(i, j)] = -coef.m[M(i, j)];
		}
	}

	return coef;
}

void Matrix4x4::invert()
{
	Matrix4x4 adj = adjoint();

	float det = determinant();

	for(int i=0; i<16; i++) {
		m[i] = adj.m[i] / det;
	}
}

void Matrix4x4::transpose()
{
	float tmp[16];

	memcpy(tmp, m, sizeof tmp);
	for(int i=0; i<4; i++) {
		for(int j=0; j<4; j++) {
			m[M(i, j)] = tmp[M(j, i)];
		}
	}
}
