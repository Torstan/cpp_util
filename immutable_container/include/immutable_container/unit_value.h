#ifndef IMMUTABLE_CONTAINER_UNIT_VALUE_H_
#define IMMUTABLE_CONTAINER_UNIT_VALUE_H_

namespace immutable_container {

struct UnitValue {
  friend bool operator==(UnitValue, UnitValue) { return true; }
  friend bool operator!=(UnitValue, UnitValue) { return false; }
};

}  // namespace immutable_container

#endif  // IMMUTABLE_CONTAINER_UNIT_VALUE_H_
