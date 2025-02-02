#pragma once
/****************************************************************************
 *
 *      bound.h: Bound and tree api for general raytracing acceleration
 *      This is part of the libYafaRay package
 *      Copyright (C) 2002  Alejandro Conty Estévez
 *
 *      This library is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU Lesser General Public
 *      License as published by the Free Software Foundation; either
 *      version 2.1 of the License, or (at your option) any later version.
 *
 *      This library is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *      Lesser General Public License for more details.
 *
 *      You should have received a copy of the GNU Lesser General Public
 *      License along with this library; if not, write to the Free Software
 *      Foundation,Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#ifndef YAFARAY_BOUND_H
#define YAFARAY_BOUND_H

#include "common/yafaray_common.h"
#include "ray.h"
#include "geometry/vector_double.h"

BEGIN_YAFARAY

/** Bounding box
 *
 * The bounding box class. A box aligned with the axis used to skip
 * object, photons, and faces intersection when possible.
 *
 */

class Bound
{
	public:
		struct Cross
		{
			bool crossed_ = false;
			float enter_;
			float leave_;
		};
		/*! Main constructor.
		 * The box is defined by two points, this constructor just takes them.
		 *
		 * @param a is the low corner (minx,miny,minz)
		 * @param g is the up corner (maxx,maxy,maxz)
		 */
		Bound(const Point3 &a, const Point3 &g) { a_ = a; g_ = g; /* null=false; */ };
		//! Default constructor
		Bound() = default;
		/*! Two child constructor.
		 * This creates a bound that includes the two given bounds. It's used when
		 * building a bounding tree
		 *
		 * @param r is one child bound
		 * @param l is another child bound
		 */
		Bound(const Bound &r, const Bound &l);
		//! Sets the bound like the constructor
		void set(const Point3 &a, const Point3 &g) { a_ = a; g_ = g; };

		//! Returns true if the given ray crosses the bound
		//bool cross(const point3d_t &from,const vector3d_t &ray) const;
		//! Returns true if the given ray crosses the bound closer than dist
		//bool cross(const point3d_t &from, const vector3d_t &ray, float dist) const;
		//bool cross(const point3d_t &from, const vector3d_t &ray, float &where, float dist) const;
		Cross cross(const Ray &ray, float t_max) const;

		//! Returns the volume of the bound
		float vol() const;
		//! Returns the lenght along X axis
		float longX() const { return g_.x() - a_.x(); }
		//! Returns the lenght along Y axis
		float longY() const { return g_.y() - a_.y(); }
		//! Returns the lenght along Z axis
		float longZ() const { return g_.z() - a_.z(); }
		//! Cuts the bound to have the given max X
		void setMaxX(float x) { g_.x() = x;};
		//! Cuts the bound to have the given min X
		void setMinX(float x) { a_.x() = x;};

		//! Cuts the bound to have the given max Y
		void setMaxY(float y) { g_.y() = y;};
		//! Cuts the bound to have the given min Y
		void setMinY(float y) { a_.y() = y;};

		//! Cuts the bound to have the given max Z
		void setMaxZ(float z) { g_.z() = z;};
		//! Cuts the bound to have the given min Z
		void setMinZ(float z) { a_.z() = z;};
		//! Adjust bound size to include point p
		void include(const Point3 &p);
		//! Returns true if the point is inside the bound
		bool includes(const Point3 &pn) const
		{
			return ((pn.x() >= a_.x()) && (pn.x() <= g_.x()) &&
					(pn.y() >= a_.y()) && (pn.y() <= g_.y()) &&
					(pn.z() >= a_.z()) && (pn.z() <= g_.z()));
		};
		float centerX() const { return (g_.x() + a_.x()) * 0.5f; }
		float centerY() const { return (g_.y() + a_.y()) * 0.5f; }
		float centerZ() const { return (g_.z() + a_.z()) * 0.5f; }
		Point3 center() const { return (g_ + a_) * 0.5f; }
		int largestAxis()
		{
			const Vec3 d{g_ - a_};
			return (d.x() > d.y()) ? ((d.x() > d.z()) ? 0 : 2) : ((d.y() > d.z()) ? 1 : 2);
		}
		void grow(float d)
		{
			a_.x() -= d;
			a_.y() -= d;
			a_.z() -= d;
			g_.x() += d;
			g_.y() += d;
			g_.z() += d;
		};

		//	protected: // Lynx; need these to be public.
		//! Two points define the box
		Point3 a_, g_;
};

inline void Bound::include(const Point3 &p)
{
	a_.x() = std::min(a_.x(), p.x());
	a_.y() = std::min(a_.y(), p.y());
	a_.z() = std::min(a_.z(), p.z());
	g_.x() = std::max(g_.x(), p.x());
	g_.y() = std::max(g_.y(), p.y());
	g_.z() = std::max(g_.z(), p.z());
}

inline Bound::Cross Bound::cross(const Ray &ray, float t_max) const
{
	// Smits method
	const Point3 &a_0{a_}, &a_1{g_};
	const Point3 p{ray.from_ - a_0};

	float lmin = -1e38f, lmax = 1e38f, ltmin, ltmax; //infinity check initial values

	if(ray.dir_.x() != 0)
	{
		float invrx = 1.f / ray.dir_.x();
		if(invrx > 0)
		{
			lmin = -p.x() * invrx;
			lmax = ((a_1.x() - a_0.x()) - p.x()) * invrx;
		}
		else
		{
			lmin = ((a_1.x() - a_0.x()) - p.x()) * invrx;
			lmax = -p.x() * invrx;
		}

		if((lmax < 0) || (lmin > t_max)) return {};
	}
	if(ray.dir_.y() != 0)
	{
		float invry = 1.f / ray.dir_.y();
		if(invry > 0)
		{
			ltmin = -p.y() * invry;
			ltmax = ((a_1.y() - a_0.y()) - p.y()) * invry;
		}
		else
		{
			ltmin = ((a_1.y() - a_0.y()) - p.y()) * invry;
			ltmax = -p.y() * invry;
		}
		lmin = std::max(ltmin, lmin);
		lmax = std::min(ltmax, lmax);

		if((lmax < 0) || (lmin > t_max)) return {};
	}
	if(ray.dir_.z() != 0)
	{
		float invrz = 1.f / ray.dir_.z();
		if(invrz > 0)
		{
			ltmin = -p.z() * invrz;
			ltmax = ((a_1.z() - a_0.z()) - p.z()) * invrz;
		}
		else
		{
			ltmin = ((a_1.z() - a_0.z()) - p.z()) * invrz;
			ltmax = -p.z() * invrz;
		}
		lmin = std::max(ltmin, lmin);
		lmax = std::min(ltmax, lmax);

		if((lmax < 0) || (lmin > t_max)) return {};
	}
	if((lmin <= lmax) && (lmax >= 0) && (lmin <= t_max))
	{
		Bound::Cross cross;
		cross.crossed_ = true;
		cross.enter_ = lmin;   //(lmin>0) ? lmin : 0;
		cross.leave_ = lmax;
		return cross;
	}

	return {};
}


class ExBound: public Bound
{
	public:
		explicit ExBound(const Bound &b)
		{
			for(int i = 0; i < 3; ++i)
			{
				center_[i] = (static_cast<double>(a_[i]) + static_cast<double>(g_[i])) * 0.5;
				half_size_[i] = (static_cast<double>(g_[i]) - static_cast<double>(a_[i])) * 0.5;
			}
		}

		Vec3Double center_;
		Vec3Double half_size_;
};


END_YAFARAY

#endif // YAFARAY_BOUND_H
