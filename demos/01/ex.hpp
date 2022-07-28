#pragma once

extern double alt_in_cm;

namespace afrl {
  namespace cmasi {
    class Location3D {
    public:
      double getAltitude() {
        return 0.0;
      }
    };
  }
}

void set_alt_in_cm(int p) {
  alt_in_cm = p;
}

int get_value() {
  int x;
  return 42 + x;
}
