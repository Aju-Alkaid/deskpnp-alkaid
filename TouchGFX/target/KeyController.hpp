#ifndef __KEYCONTROLLER_HPP__
#define __KEYCONTROLLER_HPP__

#include <platform/driver/button/ButtonController.hpp>
#include <key.h>

namespace touchgfx
{
	class KeyController : public ButtonController
	{
		public:
			KeyController(){}
			virtual ~KeyController(){}

			virtual void init();
			virtual bool sample(uint8_t& key);	
			uint8_t getKey();

	};
}

#endif