#ifndef MATRIX_H_
#define MATRIX_H_

class Matrix4x4 {
public:
	float m[16];

	Matrix4x4();
	Matrix4x4(const float *mat);
	Matrix4x4(float m00, float m01, float m02, float m03,
			float m10, float m11, float m12, float m13,
			float m20, float m21, float m22, float m23,
			float m30, float m31, float m32, float m33);
	Matrix4x4(const Matrix4x4 &mat);
	Matrix4x4 &operator =(const Matrix4x4 &mat);

	void identity();

	float determinant() const;
	Matrix4x4 adjoint() const;
	void invert();
	void transpose();


	float *operator [](int idx);
	const float *operator [](int idx) const;
};


#endif	/* MATRIX_H_ */
