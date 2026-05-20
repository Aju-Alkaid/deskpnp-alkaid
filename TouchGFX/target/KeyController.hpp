#ifndef __KEYCONTROLLER_HPP__
#define __KEYCONTROLLER_HPP__

#include <platform/driver/button/ButtonController.hpp>
#include "key.h"

namespace touchgfx
{
	class KeyController : public ButtonController
	{
	public:
		KeyController() : prevKey(0xFF) {}
		virtual ~KeyController() {}

		virtual void init();
		virtual bool sample(uint8_t& key);

	private:
		uint8_t getKey();
		uint8_t prevKey;
	};
}

#endif