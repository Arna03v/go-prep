package goconcurrencypractice


import (
	"context"
	"errors"
	"sync"
	"time"
)

// =============================================================================
// 1. Public interface
//
// Strictly one method. The caller passes their own key (IP, API key, user ID —
// the library doesn't care). Close is intentionally absent: the public contract
// is minimalist. Lifecycle is managed internally or via a type assertion.
// =============================================================================

type RateLimiter interface {
	// Allow reports whether the request identified by key is within the rate
	// limit for the current window. Returns ErrClosed if the limiter has shut
	// down, ErrRateLimitExceeded if the key has exhausted its allowance.
	Allow(ctx context.Context, key string) (bool, error)
}

// =============================================================================
// Internal per-key state — modelled as its own struct per the clarification.
//
// Keeping this separate from the map declaration makes the map readable at a
// glance and makes it easy to add fields later (e.g. last-seen timestamp for
// smarter cleanup) without changing the map type.
// =============================================================================

type keyState struct {
	// count is the number of requests recorded in the current window.
	count int

	// windowStart marks when the current window opened. The eviction check
	// compares time.Now() against windowStart + WindowDuration.
	windowStart time.Time
}

// =============================================================================
// 2. Unexported implementation struct — fields only, no method bodies
// =============================================================================

type rateLimiterImpl struct {
	// mu guards state and closed. RWMutex is on the struct because a future
	// read-only method (Stats, Peek) could hold a read lock while Allow holds
	// a write lock. Allow itself always writes (it increments count), so it
	// always takes the full write lock.
	mu sync.RWMutex

	// state holds one keyState per tracked key. The map grows as new keys are
	// seen and is periodically pruned by the cleanup ticker.
	state map[string]*keyState

	// ticker fires on CleanupInterval to trigger stale-key eviction.
	// Stored as a pointer so it can be stopped cleanly in Close.
	ticker *time.Ticker

	// once ensures the shutdown path runs exactly once.
	once sync.Once

	// closed is the shutdown flag checked at the top of every public method.
	closed bool

	// cfg is stored so Allow can read MaxRequests and WindowDuration at
	// runtime without them being re-passed on every call.
	cfg Config
}

// =============================================================================
// 3. Config struct
//
// All three fields have zero-value ambiguity — 0 means "unlimited" or
// "no window", which is almost certainly a misconfiguration. The constructor
// rejects MaxRequests <= 0 and WindowDuration <= 0 as hard errors, and
// applies a safe default for CleanupInterval if left unset.
// =============================================================================

type Config struct {
	// WindowDuration is the length of the sliding window (e.g. time.Minute).
	// Required — constructor returns an error if <= 0.
	WindowDuration time.Duration

	// MaxRequests is the maximum number of requests allowed per key per window.
	// Required — constructor returns an error if <= 0.
	MaxRequests int

	// CleanupInterval controls how often stale keys are evicted from state.
	// Defaults to 5 minutes if not set.
	CleanupInterval time.Duration
}

// =============================================================================
// 4. Constructor — defaults, validation, initialisation. No business logic.
// =============================================================================

const defaultCleanupInterval = 5 * time.Minute

// NewRateLimiter constructs a RateLimiter from cfg.
// Returns an error if MaxRequests or WindowDuration are not positive.
func NewRateLimiter(cfg Config) (RateLimiter, error) {
	if cfg.MaxRequests <= 0 {
		return nil, ErrInvalidMaxRequests
	}
	if cfg.WindowDuration <= 0 {
		return nil, ErrInvalidWindowDuration
	}
	if cfg.CleanupInterval <= 0 {
		cfg.CleanupInterval = defaultCleanupInterval
	}

	return &rateLimiterImpl{
		cfg:    cfg,
		state:  make(map[string]*keyState),
		ticker: time.NewTicker(cfg.CleanupInterval),
	}, nil
}

// =============================================================================
// 5. Sentinel error variables
//
// ErrClosed and ErrRateLimitExceeded are the two errors the public interface
// can produce. ErrInvalidMaxRequests and ErrInvalidWindowDuration are
// constructor-only validation errors — callers never see them from Allow.
// =============================================================================

var (
	// ErrClosed is returned by Allow after Close has been called.
	ErrClosed = errors.New("ratelimiter: closed")

	// ErrRateLimitExceeded is returned by Allow when the key has exhausted
	// its request allowance for the current window.
	ErrRateLimitExceeded = errors.New("ratelimiter: rate limit exceeded")

	// ErrInvalidMaxRequests is returned by NewRateLimiter when MaxRequests <= 0.
	ErrInvalidMaxRequests = errors.New("ratelimiter: MaxRequests must be greater than zero")

	// ErrInvalidWindowDuration is returned by NewRateLimiter when WindowDuration <= 0.
	ErrInvalidWindowDuration = errors.New("ratelimiter: WindowDuration must be greater than zero")
)

// =============================================================================
// 6. Compile-time interface verification
// =============================================================================

var _ RateLimiter = (*rateLimiterImpl)(nil)

// =============================================================================
// 7. Method stubs
// =============================================================================

// Allow is the single public method. It needs a full write lock — not a read
// lock — because even an allowed request must increment count on keyState.
// A read lock would only be safe for a pure inspection that never mutates.
func (r *rateLimiterImpl) Allow(ctx context.Context, key string) (bool, error) {
	r.mu.Lock()
	defer r.mu.Unlock()

	if r.closed {
		return false, ErrClosed
	}

	// Real implementation:
	//   1. Look up r.state[key]; create a new keyState if absent.
	//   2. If time.Now() is past windowStart + WindowDuration, reset count and windowStart.
	//   3. If count >= MaxRequests, return false, ErrRateLimitExceeded.
	//   4. Increment count and return true, nil.
	return true, nil
}

// Close stops the cleanup ticker and marks the limiter as shut down.
// It is on the struct but not on the RateLimiter interface — the public
// contract is intentionally one method. A caller that needs to close the
// limiter can type-assert: r.(*rateLimiterImpl).Close(), or the constructor
// can return a second, broader interface that includes Close.
func (r *rateLimiterImpl) Close() {
	r.once.Do(func() {
		r.mu.Lock()
		defer r.mu.Unlock()

		r.closed = true
		r.ticker.Stop()
		// Real teardown: nil out r.state to release memory.
	})
}