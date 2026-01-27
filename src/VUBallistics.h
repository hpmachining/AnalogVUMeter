#pragma once

class VUBallistics final {
  public:
    explicit VUBallistics(float initialDb = -20.0f);

    float process(float targetDb, float dtSeconds);
    void reset(float valueDb);

  private:
    float value_;
    float peak_;
};
