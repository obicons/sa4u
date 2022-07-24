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

double alt_in_cm;
void set_alt_in_cm(int p) {
  alt_in_cm = p;
}

int get_value() {
  int x;
  return 42 + x;
}

int main() {
  using namespace afrl;
  cmasi::Location3D loc;
  double z = loc.getAltitude();
  alt_in_cm = z * 100;

  // Fine:
  set_alt_in_cm(z * 100);
  // Error:
  set_alt_in_cm(z);

  alt_in_cm = get_value();
  // Error: inconsistent types
  // alt_in_cm = get_value() * 100;
}
