/*
    matrix3x3.h - Simple matrix operations to replace DNG SDK ones

    Copyright 2021 Alexey Danilchenko
    Written by Alexey Danilchenko

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 3, or (at your option)
    any later version with ADDITION (see below).

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, 51 Franklin Street - Fifth Floor, Boston,
    MA 02110-1301, USA.
*/
#ifndef __MATRIX_3X3__
#define __MATRIX_3X3__

#include <algorithm>
#include <cmath>
#include <cstring>

#pragma pack(push)
#pragma pack(1)
// Signed rational TIFF type
struct TSRational
{
    int32_t n;
    int32_t d;
};
#pragma pack(pop)

// Simple 3x3 matrix
class matrix3x3
{
public:
    matrix3x3(): matrix3x3(0,0,0,0,0,0,0,0,0) {}

    matrix3x3(double a00, double a01, double a02,
              double a10, double a11, double a12,
              double a20, double a21, double a22)
    {
        data[0][0] = a00; data[0][1] = a01; data[0][2] = a02;
        data[1][0] = a10; data[1][1] = a11; data[1][2] = a12;
        data[2][0] = a20; data[2][1] = a21; data[2][2] = a22;
    }

    matrix3x3(double a00, double a11, double a22): matrix3x3(a00,0,0,0,a11,0,0,0,a22) {}

    matrix3x3 (const matrix3x3 &m) { std::memcpy(data, m.data, sizeof(data)); }

    ~matrix3x3() = default;

    double* operator[](uint32_t row)  { return data[row]; }
    const double* operator[](uint32_t row) const { return data[row]; }

    double max() const
    {
        double max = data[0][0];
        for (int i=0; i<3; ++i)
            for (int j=0; j<3; ++j)
                max = std::max(max, data[i][j]);
        return max;
    }

    double min() const
    {
        double min = data[0][0];
        for (int i=0; i<3; ++i)
            for (int j=0; j<3; ++j)
                min = std::min(min, data[i][j]);
        return min;
    }

    void scale(double factor)
    {
        for (int i=0; i<3; ++i)
            for (int j=0; j<3; ++j)
                data[i][j] *= factor;
    }

    void toRational(TSRational* rm)
    {
        for (int i=0; i<3; ++i)
            for (int j=0; j<3; ++j)
                rm[i*3+j] = TSRational {(int32_t)(data[i][j]*100000), 100000};
    }

protected:

    double data[3][3];
};

// Simple vector of 3
class vector3
{
public:

    vector3(): vector3(0,0,0) {}

    vector3(double a0, double a1, double a2) { data[0] = a0; data[1] = a1; data[2] = a2; }

    vector3(const vector3 &v) { std::memcpy(data, v.data, sizeof(data)); }

    ~vector3() = default;

    double& operator[](uint32_t index) { return data[index]; }
    const double& operator[](uint32_t index) const { return data[index]; }

    double max() const { return std::max(std::max(data[0], data[1]), data[2]); }

    double min() const { return std::min(std::min(data[0], data[1]), data[2]); }

    void scale(double factor) { data[0] *= factor; data[1] *= factor; data[2] *= factor; }

    matrix3x3 asDiagMatrix() const { return matrix3x3(data[0], data[1], data[2]); }

protected:

    double data[3];
};

// Operations on matrix and vector
matrix3x3 operator*(const matrix3x3 &a, const matrix3x3 &b)
{
    matrix3x3 m;

    for (int i=0; i<3; ++i)
        for (int j=0, k=0; j<3; ++j)
            for (k=0, m[i][j]=0.0; k < 3; ++k)
                m[i][j] += a[i][k] * b[k][j];

    return m;
}

vector3 operator*(const matrix3x3 &a, const vector3 &b)
{
    vector3 v;

    for (int i=0, j=0; i<3; ++i)
        for (j=0, v[i] = 0.0; j<3; ++j)
            v[i] += a[i][j]*b[j];
    return v;
}

matrix3x3 operator+(const matrix3x3 &a, const matrix3x3 &b)
{
    matrix3x3 m(a);

    for (int i=0; i<3; ++i)
        for (int j=0; j<3; ++j)
            m[i][j] += b[i][j];

    return m;
}

matrix3x3 Invert(const matrix3x3 &a)
{
    matrix3x3 m(a[1][1]*a[2][2] - a[2][1]*a[1][2],
                a[2][1]*a[0][2] - a[0][1]*a[2][2],
                a[0][1]*a[1][2] - a[1][1]*a[0][2],
                a[2][0]*a[1][2] - a[1][0]*a[2][2],
                a[0][0]*a[2][2] - a[2][0]*a[0][2],
                a[1][0]*a[0][2] - a[0][0]*a[1][2],
                a[1][0]*a[2][1] - a[2][0]*a[1][1],
                a[2][0]*a[0][1] - a[0][0]*a[2][1],
                a[0][0]*a[1][1] - a[1][0]*a[0][1]);

    double det = (a[0][0]*m[0][0] + a[0][1]*m[1][0] + a[0][2]*m[2][0]);

    if (std::fabs(det) > 1.0E-10)
    {
        for (int i=0; i<3; ++i)
            for (int j=0; j<3; ++j)
                m[i][j] /= det;
        return m;
    }

    return a;
}

#endif
