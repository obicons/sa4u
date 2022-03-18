namespace afrl {
  namespace cmasi {
    class Location3D {
      int alt;

    public:
      // The semantic return type of this
      // method is learned from the message definitions
      // of platforms/OpenUxAS/CMASI.xml.
      // This code is auto-generated in OpenUxAS.
      double getAltitude() {
        return 0.0;
      }
    };
  }
}

// The semantic type of alt_in_cm
// would be learned using our data mining algorithm.
// alt_in_cm stores has a semantic type of centimeter.
// This information comes from ex_prior.json.
double alt_in_cm;

double alt_in_cm_local;

void update_alt_in_cm_local(double val) {
  alt_in_cm_local = val;
}

namespace n1 {
  void f(int x) {
    alt_in_cm = x;
  }
}

namespace n2 {
  void f(int x) {
    int y;
    alt_in_cm = x * 100;
  }
}

int g() {
  int x;
  return x;
}

struct parent {
  virtual void f(int x) {}
  void blah() {}
};

struct mavlink_system_time_t : public parent {
  int time_unix_usec;

  void set_time_unix_usec(int t) {
    blah();
    time_unix_usec = t;
  }

  void f(int x) {}
};

struct mavlink_set_home_position_t {
  int altitude;
};

struct c : public parent {
  int x;
  void f(int x) {
    mavlink_system_time_t t;
    mavlink_set_home_position_t h;
  }
};

struct mavlink_obstacle_distance_t {
  unsigned char frame;
  int min_distance;
};

int main() {
  using namespace afrl;
  using namespace n1;
  cmasi::Location3D loc;

  // The return type of loc.getAltitude() is m.
  double z = loc.getAltitude();
  alt_in_cm = z * 100.0;

  // Okay.
  alt_in_cm = -alt_in_cm;

  // Not okay.
  //alt_in_cm = (-loc.getAltitude());

  // Namespaces work correctly.
  f(z * 100);

  // Error, because z is multiplied by 100 2x.
  // n2::f(z * 100);

  // This is okay. We'll infer the return type of g().
  alt_in_cm = g();

  // Also okay. We'll infer the return type of g().
  int x = g();

  // Not okay. Either alt_in_cm = g() is illegal or the next line is illegal.
  //alt_in_cm = x * 100;

  // Okay. alt_in_cm and x share g's return type.
  // Casts work too.
  alt_in_cm = (int) x;

  // Error: alt_in_cm_local has a different frame.
  // update_alt_in_cm_local(alt_in_cm);

  mavlink_system_time_t time;

  // Okay. Obviously, LHS and RHS share a type.
  time.time_unix_usec = time.time_unix_usec;

  // Error: time is known from MAVLink file.
  // time.time_unix_usec = alt_in_cm;
  // time.set_time_unix_usec(alt_in_cm);

  // Okay. We'll infer p->f's argument type.
  struct parent *p = &time;
  p->f(x * 100);

  struct c c1;
  struct parent *p1 = &c1;
  // Not okay. Doesn't match p->f's argument type.
  // p1->f(alt_in_cm);

  int *arr = new int[2];
  // Okay. We'll infer arr[i]'s type.
  arr[0] = alt_in_cm;

  // Error: type(arr[0]) != type(arr[1])
  //arr[1] = alt_in_cm * 100;

  mavlink_obstacle_distance_t dist;
  if (dist.frame == 0) {
    // This is okay, since we know the frame of dist.
    alt_in_cm = dist.min_distance;
  }

  // This is not okay, since dist could have any frame.
  // alt_in_cm = dist.min_distance;

  // This is okay, since it proves that dist.frame must be correct.
  if (dist.frame != 0) {
    return;
  }
  alt_in_cm = dist.min_distance;
}
