#include "linkerhand/hand/l6/l6.hpp"

namespace linkerhand::hand::l6 {

L6::L6(std::string_view side, std::string_view interface_name, std::string_view interface_type)
    : lifecycle_(std::make_shared<linkerhand::Lifecycle>("L6")),
      dispatcher_(interface_name, interface_type),
      arbitration_id_(side == "right" ? 0x27 : 0x28),
      angle(arbitration_id_, dispatcher_, lifecycle_),
      force_sensor(arbitration_id_, dispatcher_, lifecycle_) {}

L6::~L6() {
  try {
    close();
  } catch (...) {
  }
}

void L6::close() {
  if (!lifecycle_->close()) {
    return;
  }
  try {
    force_sensor.stop_streaming();
    angle.stop_streaming();
  } catch (...) {
  }
  try {
    dispatcher_.stop();
  } catch (...) {
  }
}

bool L6::is_closed() const {
  return lifecycle_->is_closed();
}

}  // namespace linkerhand::hand::l6
