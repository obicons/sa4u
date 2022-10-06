namespace afrl {
namespace cmasi {
class Location3D {
public:
  double getAltitude() { return 0.0; }
};
} // namespace cmasi
} // namespace afrl

struct my_struct {
  double z;
};

double alt_in_cm = 100.0;
void set_alt_in_cm(int p) { alt_in_cm = p; }

int get_value() {
  int x;
  return 42 + x;
}

static double altitude_converter(double p) { return p / 1000.0; }

int main() {
  using namespace afrl;
  cmasi::Location3D loc;
  double z = loc.getAltitude();

  // A conversion is alright.
  alt_in_cm = z * 100;

  // This is right, for all we know.
  alt_in_cm = 1000;

  // Fine:
  set_alt_in_cm(z * 100);
  // Error:
  // set_alt_in_cm(z);

  // This is also an error, but now it's okay because we use an ignore
  // directive.
  // @sa4u.ignore
  set_alt_in_cm(z);

  alt_in_cm = get_value();
  // Error: inconsistent types
  // alt_in_cm = get_value() * 100;

  double f = z;
  // Error: inconsistent types.
  // alt_in_cm = f;

  struct my_struct ms;
  ms.z = f;
  // Error: inconsistent types.
  // alt_in_cm = ms.z;

  altitude_converter(alt_in_cm);
  // Error: inconsistent types.
  // altitude_converter(ms.z);

  // Note that this is likely an error.
  // However, we have to choose between completeness and soundness.
  // So, this is tolerated.
  alt_in_cm = altitude_converter(alt_in_cm);

  // Even though the previous line is allowed, we can still detect
  // inconsistencies.
  // Error:
  // ms.z = altitude_converter(alt_in_cm);
}
