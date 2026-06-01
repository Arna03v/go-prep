package finalconnectionpool

import (
	"context"
	"net"
)

// Pool manages a fixed set of reusable network connections.
type Pool interface {
    // Acquire returns a connection from the pool.
    // Blocks if no connection is available.
    // Returns error if context is cancelled or times out before
    // a connection becomes available.
    Acquire(ctx context.Context) (net.Conn, error)

    // Release returns a connection to the pool.
    // Signals waiting goroutines that a connection is available.
    Release(conn net.Conn)

    // Destroy discards a connection permanently — used when a
    // connection encounters a network failure and cannot be reused.
    // Opens a slot in the pool for a new connection to be created.
    Destroy(conn net.Conn) error

    // Close shuts down the pool, closes all idle connections,
    // and prevents new Acquire calls.
    Close() error
}

type poolImpl struct{
	// needs a fixed size
	// can be taken from the input and not stored permanently
	// max_size int

	// tries to acquaire a connection;
	// check for connections doing nothing
		// return 
	// if none; then check for max number of connections
		// if can create then create else wait

	// but this might be a little complicated, we can probably scope it to having the max connections already waiting in the connection pool. is that okay?

		// laziliy on demand; means that if we dont require the full power of the connection pool then we are not initilaising it. saving on compute. teh cons are that teh first time the connection is being spooled up; it will be slow.

		// thinking of creating upfront and storing it. 
			// reduces complexity while still acheiving the goals
			// efficiency wasnt a mentioned goal

	// we will spin up the connections
	// the connections can live in a buffered channel of net.conn
	// if the channel == empty; needs to wait before acquriing
	// if the channel is full; needs to wait before pushing
	// during destory
		// do we make another connection to replace this?
			// yes do it in background

	// release will just add it back to the pool
	// for close
	// all functions can listen to a done channel
	// the close() closes the done channel and stops taking in new connections

	idle_connections chan net.Conn
	ctx context.Context
	cancel context.CancelFunc
	// to make the closing of the done channel idempotent

	// also need something to create the connection when it fails
	factory func() (net.Conn, error)
}


// next would be to figure out how to pass the paramaters. which in this case are the max_size and the factory function. 

// since there are only 2 i will pass it dorectly via the constructor

// ideally i would like to have error type 

// ```
// var (
//   errPoolClosed = errors.New("connection pool closed)
// )
// ```

// but in the interest of time i will skip those. will mention to the interviewer


// constructor will validate teh inputs
// set the parameters
// spin up connection and push to the channel
func NewPool(max_size int, factory_func func()(net.Conn,error)) (*Pool, error){
	if max_size <= 0 {
		return errors.new ("invalid param")
	}

	p := &poolImpl{}
	p.idle_connections = make(chan net.Conn, max_size)
	p.ctx, p.cancel = context.WithCancel(context.Background())
	p.factory = factory_func

	for i:=0; i<max_size; i++{
		conn, err := factory_func()
		if err != nil{
			return nil, errors.new("error creating conection")
		}

		p.idle_connections <- conn
	}

	return p, nil
}

func (p *poolImpl) Acquire(ctx context.Context) (net.Conn, error){
	// if the conext expires return err
	// if pool is closed return error
	// if the channel is blocked wait
		// until channel is unblocked 
		// return the connection

	select{
	case <- p.ctx.done:
		return nil, errors.new("pool closed not accepting anymore connections")

	case <- ctx.Done():
		return nil, errors.new("timeout... could not establsh connection")

	case conn := <-p.idle_connections:
		return conn, nil
	}
}

func (p *poolImpl) Release(conn net.Conn){
	// if the pool is closed then nothing to push into
	// if the channel is fulll then wait until done
		// wait in the background; the user does not need to be blocked

	// no since we create a fixed number of connections and only replace when destoryed
		// no need to wait for full channel

	select{
	case <- p.ctx.done:
		// cant put it back
		// connection is a file descriptor; leaving it causes memory leak
		close_file_descriptor(conn); // assume this function closes teh file descriptor

	default:
		p.idle_connections <- conn
	}
}

func (p *poolImpl) Destroy(conn net.Conn) error{
	// close this connection.
	close_file_descriptor(conn)
	// check if pool is open
		// if yes spin up another and push
		// lets do the spin up before so that we dont wait after the default block is executed

	// doing sync means we wait on the factory which can have high chance of failing
		// async means we run till timeout and potentially lose a connection
	
	new_conn, err := p.factory()
	// handle err != nil

	select{
	case <- p.ctx.done:
		// nothing to push to
		close_file_descriptor(new_conn)
		return nil
	default:
		p.idle_connections<- new_conn
		return nil
	}

}

func (p *poolImpl) Close() error{
	p.cancel()
	// consume each connection and close the file descriptor
	// then close the channel

}

