#include "geometry.h"

Crit3DGeometry::Crit3DGeometry()
{
    this->clear();
}


void Crit3DGeometry::clear()
{
    m_showHiddenFace = false,
    m_dataCount = 0;
    m_colorCount = 0;
    m_xCenter = 0;
    m_yCenter = 0;
    m_zCenter = 0;
    m_dx = 0;
    m_dy = 0;
    m_magnify = 1;

    m_data.clear();
    m_colors.clear();
}


void Crit3DGeometry::setCenter(float x, float y, float z)
{
    m_xCenter = x;
    m_yCenter = y;
    m_zCenter = z;
}

void Crit3DGeometry::setDimension(float dx, float dy)
{
    m_dx = dx;
    m_dy = dy;
}

void Crit3DGeometry::addTriangle(const QVector3D &p1, const QVector3D &p2, const QVector3D &p3,
                                 const Crit3DColor &c1, const Crit3DColor &c2, const Crit3DColor &c3)
{
    QVector3D n = QVector3D::normal(QVector3D(p2 - p1), QVector3D(p3 - p1));
    addVertex(p1, n);
    addVertexColor(c1);
    addVertex(p2, n);
    addVertexColor(c2);
    addVertex(p3, n);
    addVertexColor(c3);

    if (m_showHiddenFace)
    {
        n = QVector3D::normal(QVector3D(p2 - p3), QVector3D(p1 - p3));
        addVertex(p3, n);
        addVertexColor(c3);
        addVertex(p2, n);
        addVertexColor(c2);
        addVertex(p1, n);
        addVertexColor(c1);
    }
}

void Crit3DGeometry::addVertex(const QVector3D &v, const QVector3D &normal)
{
    m_data.append(v.x() - m_xCenter);
    m_data.append(v.y() - m_yCenter);
    m_data.append((v.z() - m_zCenter) * m_magnify);
    m_data.append(normal.x());
    m_data.append(normal.y());
    m_data.append(normal.z());

    m_dataCount += 6;
}

void Crit3DGeometry::addVertexColor(const Crit3DColor &color)
{
    m_colors.append(float(color.red) / 255.f);
    m_colors.append(float(color.green) / 255.f);
    m_colors.append(float(color.blue) / 255.f);

    m_colorCount += 3;
}

void Crit3DGeometry::setVertexColor(int i, const Crit3DColor &color)
{
    if (i > m_colorCount / 3) return;

    m_colors[i*3] = float(color.red) / 255.f;
    m_colors[i*3+1] = float(color.green) / 255.f;
    m_colors[i*3+2] = float(color.blue) / 255.f;
}
