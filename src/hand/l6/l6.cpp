#include "linkerhand/hand/l6/l6.hpp"

namespace linkerhand::hand::l6 {

L6::L6(const std::string& side, const std::string& interface_name, const std::string& interface_type)
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
  if (lifecycle_->is_closed()) {
    return;
  }

  try {
    force_sensor.stop_streaming();
    angle.stop_streaming();
  } catch (...) {
  }

  lifecycle_->begin_close();
  lifecycle_->notify_closing();

  try {
    dispatcher_.stop();
  } catch (...) {
  }
  lifecycle_->finish_close();
}

bool L6::is_closed() const { return lifecycle_->is_closed(); }

void L6::ensure_open() const {
  lifecycle_->ensure_open();
}

}  // namespace linkerhand::hand::l6
