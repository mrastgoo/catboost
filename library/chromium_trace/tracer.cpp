#include "tracer.h"

#include <library/json/json_writer.h>

namespace NChromiumTrace {
    void TTracer::NotifySuppressedException() {
        static bool messageWritten = false;
        if (!messageWritten) {
            Cerr << "WARNING: Exception in trace consumer. " << CurrentExceptionMessage() << " (further messages will be suppressed)" << Endl;
            messageWritten = true;
        }
    }

    void TTracer::AddDurationBeginNow(TStringBuf name, TStringBuf cat) noexcept {
        if (!Output)
            return;

        SuppressExceptions([&] {
        Output->AddEvent(TDurationBeginEvent {
            TEventOrigin::Here(),
            name,
            cat,
            TEventTime::Now(),
            TEventFlow {EFlowType::None, 0},
        }, nullptr);
        });
    }

    void TTracer::AddDurationEndNow() noexcept {
        if (!Output)
            return;

        SuppressExceptions([&] {
        Output->AddEvent(TDurationEndEvent {
            TEventOrigin::Here(),
            TEventTime::Now(),
            TEventFlow {EFlowType::None, 0},
        }, nullptr);
        });
    }

    TMaybe<TDurationCompleteEvent> TTracer::BeginDurationCompleteNow(TStringBuf name, TStringBuf cat) noexcept {
        if (!Output)
            return Nothing();

        return TDurationCompleteEvent{
            TEventOrigin::Here(),
            name,
            cat,
            TEventTime::Now(),
            TEventTime(),
            TEventFlow {EFlowType::None, 0},
        };
    }

    void TTracer::EndDurationCompleteNow(TDurationCompleteEvent& event) noexcept {
        event.EndTime = TEventTime::Now();
        AddEvent(event);
    }

    void TTracer::AddCounterNow(TStringBuf name, TStringBuf cat, const TEventArgs& args) noexcept {
        if (!Output)
            return;

        SuppressExceptions([&] {
        Output->AddEvent(TCounterEvent {
            TEventOrigin::Here(),
            name,
            cat,
            TEventTime::Now(),
        }, &args);
        });
    }

    void TTracer::AddCurrentProcessName(TStringBuf name) noexcept {
        if (!Output)
            return;

        SuppressExceptions([&] {
        Output->AddEvent(TMetadataEvent {
            TEventOrigin::Here(),
            STRINGBUF("process_name"),
        }, &TEventArgs().Add(STRINGBUF("name"), name));
        });
    }

    void TTracer::AddCurrentThreadName(TStringBuf name) noexcept {
        if (!Output)
            return;

        SuppressExceptions([&] {
        Output->AddEvent(TMetadataEvent {
            TEventOrigin::Here(),
            STRINGBUF("thread_name"),
        }, &TEventArgs().Add(STRINGBUF("name"), name));
        });
    }

    void TTracer::AddCurrentThreadIndex(i64 index) noexcept {
        if (!Output)
            return;

        SuppressExceptions([&] {
        Output->AddEvent(TMetadataEvent {
            TEventOrigin::Here(),
            STRINGBUF("thread_sort_index"),
        }, &TEventArgs().Add(STRINGBUF("sort_index"), index));
        });
    }

} // namespace NChromiumTrace
