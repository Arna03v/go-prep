package goconcurrencypractice


import (
	"context"
	"errors"
	"net"
	"sync"
	"time"
)

// =============================================================================
// 1. Public interface
//
// The contract callers depend on. The constructor returns this type — callers
// never see poolImpl. Only what the caller needs is exposed here.
// =============================================================================

type Pool interface {
	// Acquire returns an idle connection, creating one via the factory if the
	// pool is empty. Blocks until ctx is cancelled or a connection is available.
	Acquire(ctx context.Context) (net.Conn, error)

	// Release returns a healthy connection to the pool for reuse.
	Release(conn net.Conn) error

	// Destroy discards a connection that encountered a network failure.
	// The caller marks it as unusable rather than returning it.
	Destroy(conn net.Conn) error

	// Close drains the pool and prevents new Acquire calls. Idempotent.
	Close() error
}

// =============================================================================
// 2. Unexported implementation struct — fields only, no method bodies
//
// Callers cannot construct this directly; they go through NewPool.
// =============================================================================

type poolImpl struct {
	// if we have an idle connection; use it by picking it from this buffered channel
	// else create a new connection
	conns chan net.Conn

	// mu guards closed and any other mutable scalar 
	mu sync.Mutex

	// once ensures Close teardown runs exactly once, even under concurrent calls.
	once sync.Once

	// closed is the shutdown flag. Every public method checks this first.
	closed bool

	// maxSize is the upper bound on the number of idle-connections the pool holds.
	maxSize int

	// idleTimeout is how long a connection may sit idle before being evicted.
	// The eviction goroutine would read this; stored here so options can set it.
	idleTimeout time.Duration

	// factory is the caller-provided function that creates new net.Conn values.
	// Required; NewPool returns ErrNoFactory if it is nil.
	factory func() (net.Conn, error)
}

// =============================================================================
// 3. Functional options
//
// The exercise specifies functional options (not a Config struct) for this
// library. Each option is a closure that mutates the poolImpl before validation.
// Trade-off vs Config struct: options eliminate zero-value ambiguity and can
// set multiple related fields atomically, at the cost of slightly more boilerplate.
// =============================================================================

// Option is a functional option for NewPool.
type Option func(*poolImpl)

// WithMaxSize sets the maximum number of connections the pool will hold.
// Defaults to defaultMaxSize if not supplied.
func WithMaxSize(n int) Option {
	return func(p *poolImpl) {
		p.maxSize = n
	}
}

// WithIdleTimeout sets how long a connection may be idle before eviction.
// Defaults to zero (no timeout) if not supplied.
func WithIdleTimeout(d time.Duration) Option {
	return func(p *poolImpl) {
		p.idleTimeout = d
	}
}

// WithFactory provides the function the pool calls to create new connections.
// This option is required; omitting it causes NewPool to return ErrNoFactory.
func WithFactory(f func() (net.Conn, error)) Option {
	return func(p *poolImpl) {
		p.factory = f
	}
}

// =============================================================================
// 4. Constructor — defaults, validation, and initialisation. No business logic.
// =============================================================================

const defaultMaxSize = 10

// NewPool constructs a Pool with the provided options.
// Returns ErrNoFactory if no factory function was supplied.
// Returns ErrInvalidSize if maxSize resolves to ≤ 0.
func NewPool(opts ...Option) (Pool, error) {
	p := &poolImpl{
		maxSize: defaultMaxSize, // applied before options so callers can override
	}

	for _, opt := range opts {
		opt(p)
	}

	// Validate: factory is required — the pool cannot create connections without it.
	if p.factory == nil {
		return nil, ErrNoFactory
	}

	// Validate: a non-positive size produces an unusable channel.
	if p.maxSize <= 0 {
		return nil, ErrInvalidSize
	}

	// Initialise internal state now that config is validated.
	p.conns = make(chan net.Conn, p.maxSize)

	return p, nil
}

// =============================================================================
// 5. Sentinel error variables
//
// ErrClosed and ErrPoolExhausted are the errors the exercise specifies.
// ErrNoFactory and ErrInvalidSize are constructor-validation errors.
// Sentinels are used here because callers only need identity checks (errors.Is),
// not attached data — a custom type would be warranted if RetryAfter or similar
// context were needed.
// =============================================================================

var (
	// ErrClosed is returned by any method called after Close().
	ErrClosed = errors.New("pool: closed")

	// ErrPoolExhausted is returned by Acquire when all connections are in use
	// and the pool cannot create a new one without exceeding maxSize.
	ErrPoolExhausted = errors.New("pool: exhausted")

	// ErrNoFactory is returned by NewPool when no WithFactory option was provided.
	ErrNoFactory = errors.New("pool: factory function is required")

	// ErrInvalidSize is returned by NewPool when maxSize resolves to ≤ 0.
	ErrInvalidSize = errors.New("pool: max size must be greater than zero")
)

// =============================================================================
// 6. Compile-time interface verification
//
// If poolImpl is missing any method declared on Pool, this line produces a
// compile error. poolImpl stays unexported; the Pool interface is the public face.
// =============================================================================

var _ Pool = (*poolImpl)(nil)

// =============================================================================
// 7. Method stubs — bodies are intentionally empty per the exercise spec.
//    The closed check and sync.Once patterns are the focus, not the logic.
// =============================================================================

func (p *poolImpl) Acquire(ctx context.Context) (net.Conn, error) {
	p.mu.Lock()
	closed := p.closed
	p.mu.Unlock()

	if closed {
		return nil, ErrClosed
	}

	// Real implementation: select on p.conns, ctx.Done(), and a factory branch.
	return nil, nil
}

func (p *poolImpl) Release(conn net.Conn) error {
	p.mu.Lock()
	closed := p.closed
	p.mu.Unlock()

	if closed {
		return ErrClosed
	}

	// Real implementation: attempt non-blocking send to p.conns; close conn on overflow.
	return nil
}

func (p *poolImpl) Destroy(conn net.Conn) error {
	p.mu.Lock()
	closed := p.closed
	p.mu.Unlock()

	if closed {
		return ErrClosed
	}

	// Real implementation: conn.Close() and decrement an in-use counter.
	return nil
}

// Close shuts the pool down. sync.Once guarantees teardown runs exactly once
// regardless of how many goroutines call Close concurrently.
func (p *poolImpl) Close() error {
	p.once.Do(func() {
		p.mu.Lock()
		defer p.mu.Unlock()

		p.closed = true

		// Real teardown: drain p.conns, close each net.Conn, close the channel.
	})

	return nil
}