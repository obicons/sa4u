namespace afrl {
  namespace cmasi {
    class Location3D {
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
double alt_in_cm = 0.0;

int main() {
  using namespace afrl;
  cmasi::Location3D loc;
  double z = loc.getAltitude();
  alt_in_cm = z;
}
