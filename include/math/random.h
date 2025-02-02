/****************************************************************************
 *
 *      This is part of the libYafaRay package
 *      Copyright (C) 2002 Alejandro Conty Estévez
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

#ifndef YAFARAY_RANDOM_H
#define YAFARAY_RANDOM_H

#include "common/yafaray_common.h"
#include "math/math.h"

BEGIN_YAFARAY

class FastRandom final
{
	public:
		static int getNextInt();
		static int getNextInt(int &seed);
		static float getNextFloatNormalized();
		static float getNextFloatNormalized(int &seed);

	private:
		static int myseed_; //FIXME: Should this be std::atomic<int> for thread safety? Performance??
		static constexpr int a_ = 0x000041A7;
		static constexpr int m_ = 0x7FFFFFFF;
		static constexpr int q_ = 0x0001F31D; // m/a
		static constexpr int r_ = 0x00000B14; // m%a;

};

/* multiply-with-carry generator x(n) = a*x(n-1) + carry mod 2^32.
   period = (a*2^31)-1 */

/* Choose a value for a from this list
   1791398085 1929682203 1683268614 1965537969 1675393560
   1967773755 1517746329 1447497129 1655692410 1606218150
   2051013963 1075433238 1557985959 1781943330 1893513180
   1631296680 2131995753 2083801278 1873196400 1554115554
*/
class RandomGenerator final
{
	public:
		RandomGenerator() = default;
		explicit RandomGenerator(unsigned int seed): c_(seed) { }
		double operator()();
	protected:
		unsigned int x_ = 30903, c_ = 0;
		static constexpr unsigned int y_a_ = 1791398085;
		static constexpr unsigned int y_ah_ = (y_a_ >> 16);
		static constexpr unsigned int y_al_ = y_a_ & 65535;

};


inline int FastRandom::getNextInt()
{
	myseed_ = a_ * (myseed_ % q_) - r_ * (myseed_ / q_);
	if(myseed_ < 0)
		myseed_ += m_;
	return myseed_;
}

inline float FastRandom::getNextFloatNormalized()
{
	return static_cast<float>(getNextInt()) / static_cast<float>(m_);
}

inline int FastRandom::getNextInt(int &seed)
{
	seed = a_ * (seed % q_) - r_ * (seed / q_);
	if(myseed_ < 0)
		myseed_ += m_;
	return seed;
}

inline float FastRandom::getNextFloatNormalized(int &seed)
{
	return static_cast<float>(getNextInt(seed)) / static_cast<float>(m_);
}

inline double RandomGenerator::operator()()
{
	const unsigned int xh = x_ >> 16, xl = x_ & 65535;
	x_ = x_ * y_a_ + c_;
	c_ = xh * y_ah_ + ((xh * y_al_) >> 16) + ((xl * y_ah_) >> 16);
	if(xl * y_al_ >= ~c_ + 1) c_++;
	return static_cast<double>(x_) * math::sample_mult_ratio;
}


END_YAFARAY

#endif //YAFARAY_RANDOM_H
