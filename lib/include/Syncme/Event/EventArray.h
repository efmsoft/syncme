#pragma once

#include <memory>
#include <vector>

#include <Syncme/Api.h>

namespace Syncme
{
  class Event;
  typedef std::shared_ptr<Syncme::Event> HEvent;
  
  struct EventArray : public std::vector<HEvent>
  {
    SINCMELNK EventArray(
      HEvent e1 = HEvent()
      , HEvent e2 = HEvent()
      , HEvent e3 = HEvent()
      , HEvent e4 = HEvent()
      , HEvent e5 = HEvent()
      , HEvent e6 = HEvent()
      , HEvent e7 = HEvent()
      , HEvent e8 = HEvent()
    );
 };
}