#pragma once

#include <FramedSource.hh>

#include <string>

class H265FramedSource: public FramedSource {
public:
  static H265FramedSource *createNew(UsageEnvironment& env, unsigned fps, 
      std::string input_file, std::string result_file);

public:
  static EventTriggerId eventTriggerId;
  // Note that this is defined here to be a static class variable, because this code is intended to illustrate how to
  // encapsulate a *single* device - not a set of devices.
  // You can, however, redefine this to be a non-static member variable.
  void deliver_frame();

protected:
  H265FramedSource(UsageEnvironment& env, unsigned fps, std::string input_file, std::string result_file);
  // called only by createNew(), or by subclass constructors
  virtual ~H265FramedSource();

private:
  // redefined virtual functions:
  virtual void doGetNextFrame();
  //virtual void doStopGettingFrames(); // optional

private:
  static void deliverFrame0(void* clientData);
  void deliverFrame();

private:
  static unsigned referenceCount; // used to count how many instances of this class currently exist
  unsigned fps_;

  std::string input_file_;
  std::string result_file_;
};