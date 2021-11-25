#pragma once

#include <FramedSource.hh>

class H265LatencyFramedSource : public FramedSource {
    public:
        static H265LatencyFramedSource *createNew(UsageEnvironment& env, std::string input_file);
        static EventTriggerId eventTriggerId;

    protected:
        H265LatencyFramedSource(UsageEnvironment& env, std::string input_file);
        virtual ~H265LatencyFramedSource();

    private:
        void deliverFrame();
        virtual void doGetNextFrame();
        static void deliverFrame0(void *clientData);

        static unsigned referenceCount;

        std::string input_file_;
};
