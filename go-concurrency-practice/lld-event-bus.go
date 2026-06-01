package goconcurrencypractice

import (
	"context"
	"errors"
	"sync"
)

// =============================================================================
// 1. Public interface
//
// Two clear concerns: publishing events and subscribing handlers. Both live on
// the same interface — callers use one type for the whole bus.
// =============================================================================

type EventBus interface {
	// Publish sends payload to every subscriber registered under topic.
	// Returns ErrClosed if the bus has shut down, ErrTopicNotFound if nobody
	// has subscribed to that topic yet.
	Publish(topic string, payload any) error

	// Subscribe registers handler under topic. The handler is called for every
	// future Publish on that topic. Returns ErrSubscriberLimitReached if the
	// topic already has MaxSubscribersPerTopic handlers.
	Subscribe(topic string, handler func(payload any)) error

	// Close signals shutdown and blocks until all enqueued events finish
	// processing or ctx expires, whichever comes first.
	Close(ctx context.Context) error
}

// =============================================================================
// 2. Unexported implementation struct — fields only, no method bodies
// =============================================================================

type eventBusImpl struct {
	// mu guards topics and closed. RWMutex because Publish only reads the map
	// (many concurrent publishes on different topics are safe) while Subscribe
	// and Close write to it.
	// better to have one mutex per channel
	mu sync.RWMutex

	// topics maps each topic name to one buffered channel per subscriber.
	// A slice of channels, not a single channel, is the key design choice:
	// each subscriber gets its own channel and its own draining goroutine, so
	// a slow handler on one subscription cannot block any other subscription.
	topics map[string][]chan any

	// wg tracks all active handler goroutines. Close calls wg.Wait() so
	// it can block until every in-flight event has finished processing.
	wg sync.WaitGroup

	// once ensures the shutdown path — closing channels, setting closed — runs
	// exactly once even if Close is called concurrently from multiple goroutines.
	once sync.Once

	// closed is the shutdown flag. Every public method checks this first.
	closed bool

	// cfg holds the validated configuration. Stored on the struct because
	// Subscribe needs BufferPerTopic when creating new channels, and needs
	// MaxSubscribersPerTopic when enforcing the per-topic subscriber cap.
	cfg Config
}

// =============================================================================
// 3. Config struct
//
// Both fields have obvious zero-value ambiguity (0 means "no buffer" and
// "no subscribers allowed"), so the constructor applies defaults for anything
// left at zero. Callers fill in only what they care about.
// =============================================================================

type Config struct {
	// BufferPerTopic is the capacity of each subscriber's channel.
	// Defaults to 64 if not set.
	BufferPerTopic int

	// MaxSubscribersPerTopic caps how many handlers can register on one topic.
	// Defaults to 16 if not set.
	MaxSubscribersPerTopic int
}

// =============================================================================
// 4. Constructor — defaults, validation, initialisation. No business logic.
// =============================================================================

const (
	defaultBufferPerTopic         = 64
	defaultMaxSubscribersPerTopic = 16
)

// NewEventBus constructs an EventBus with the provided config.
// Zero values in cfg are replaced with safe defaults.
func NewEventBus(cfg Config) (EventBus, error) {
	if cfg.BufferPerTopic <= 0 {
		cfg.BufferPerTopic = defaultBufferPerTopic
	}
	if cfg.MaxSubscribersPerTopic <= 0 {
		cfg.MaxSubscribersPerTopic = defaultMaxSubscribersPerTopic
	}

	return &eventBusImpl{
		cfg:    cfg,
		topics: make(map[string][]chan any),
	}, nil
}

// =============================================================================
// 5. Sentinel error variables
//
// All three are identity-check errors — callers use errors.Is, they don't need
// attached data. A custom type would only be warranted if, say, ErrTopicNotFound
// needed to carry the topic name for logging.
// =============================================================================

var (
	// ErrClosed is returned by any method called after Close().
	ErrClosed = errors.New("eventbus: closed")

	// ErrTopicNotFound is returned by Publish when no handler has subscribed
	// to the given topic.
	ErrTopicNotFound = errors.New("eventbus: topic not found")

	// ErrSubscriberLimitReached is returned by Subscribe when a topic already
	// has MaxSubscribersPerTopic handlers registered.
	ErrSubscriberLimitReached = errors.New("eventbus: subscriber limit reached")
)

// =============================================================================
// 6. Compile-time interface verification
// =============================================================================

var _ EventBus = (*eventBusImpl)(nil)

// =============================================================================
// 7. Method stubs — bodies are intentionally skeletal.
//    The closed checks, RWMutex usage, and WaitGroup/Once patterns are the focus.
// =============================================================================

func (b *eventBusImpl) Publish(topic string, payload any) error {
	// Read lock: multiple goroutines can publish to different topics concurrently.
	b.mu.RLock()
	closed := b.closed
	channels := b.topics[topic]
	b.mu.RUnlock()

	if closed {
		return ErrClosed
	}
	if channels == nil {
		return ErrTopicNotFound
	}

	// Real implementation: non-blocking send to each channel; drop or error on full.
	return nil
}

func (b *eventBusImpl) Subscribe(topic string, handler func(payload any)) error {
	// Write lock: modifying the topics map.
	b.mu.Lock()
	defer b.mu.Unlock()

	if b.closed {
		return ErrClosed
	}
	if len(b.topics[topic]) >= b.cfg.MaxSubscribersPerTopic {
		return ErrSubscriberLimitReached
	}

	ch := make(chan any, b.cfg.BufferPerTopic)
	b.topics[topic] = append(b.topics[topic], ch)

	// Each subscriber gets its own goroutine draining its own channel.
	// This is what isolates topics: a slow handler blocks only its own goroutine.
	b.wg.Add(1)
	go func() {
		defer b.wg.Done()
		for payload := range ch {
			handler(payload)
		}
	}()

	return nil
}

// Close shuts the bus down in two phases.
//
// Phase 1 (once-guarded): set closed and close every subscriber channel.
// Closing a channel causes the range loop in each subscriber goroutine to exit
// after draining whatever is already buffered — no events are dropped mid-flight.
//
// Phase 2: wait for all goroutines to finish, but respect the context deadline.
// Phase 2 runs on every call to Close, not just the first, so a caller that
// passes a very short deadline can retry with a longer one.
func (b *eventBusImpl) Close(ctx context.Context) error {
	b.once.Do(func() {
		b.mu.Lock()
		defer b.mu.Unlock()

		b.closed = true

		for _, channels := range b.topics {
			for _, ch := range channels {
				close(ch)
			}
		}
	})

	// Wrap wg.Wait() in a goroutine so we can race it against ctx.Done().
	done := make(chan struct{})
	go func() {
		b.wg.Wait()
		close(done)
	}()

	select {
	case <-done:
		return nil
	case <-ctx.Done():
		return ctx.Err()
	}
}