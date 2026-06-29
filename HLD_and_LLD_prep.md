# HLD
Watching 4 videos
# Basic approach
1. ask clarifying questions (functional requirements)
- constrain the problem to solve within the hour
- share what we know about the system and then ask what to focus on

2. High level metrics (non-functional requirements)
- specify some key metrics to do some math to get an idea of
    - how much data we will be dealing with
    - what sort of throughput will be required

- Dont get stuck for too long asking questions. Generally only 3 values are required
    - Daily active users
    - Total data
    - Throughput (songs streamed per day, downloads per day type stuff)

3. Can also help to list non-requirements (stuff that is present in the current applications that we will not be working on)

3. Can define the APIs before the system
    - what operations; for each of them we wnt the 
        - endpoint
        - data sent in the request
        - data received

3. Layout the basic components of our design
- need to be able to communicate well by conversation as well as drawing
- can start with a simpler design initially and refine it (maybe with more components) as the interview progresses

4. Define the database layout
- Need to figure out what to store. Starting from the start of the flow helps
- Different data types and access patterns warrant for different ways to store and access them 
    - Blob storage : storing immutable files (pdfs, audio, images, etc). Generally used to store stuff that will be streamed. The object itself will not be updated
        - amazon s3 : allows us to tier data so that more frequently used are stored in a hot path

    - NoSQL key-value storage : NoSQL because we want the data to be denormalised (might not have a specific schema), might want faster reads. NoSQL focuses on **BASE** (basically available, soft state, eventually consistent)
        - amazon dynamo db
            - better to start out quickly, set up later
            - built in features for data partitioning, based on consistent hashing
            - has indexing as well
    
    - Relational data store : Need strict consistency and availability, can do queries and the data has relationships among themselves. It is much harder to scale than NoSQL (need to scale vertically, horizonatal scaling leads to very poor join perf)

5. Also define what the databases will store, even if brief. Helps in clarity of the example

6. Run through a use-case and refine where needed/name possible issues with the implementation

7. Wrap up the discussion by outlining how the design meets the requirements. 
- End of interview is a chance to think big and introduce a new dimension!

- Akamai and AWS cloudfront are CDNs. Not cloudflare

# [Design spotify](https://www.youtube.com/watch?v=_K-eupuDVEc&list=PLf3F6FcQwgqEpnucyupbIqzxyvFOz9uDq&index=1)

## Clarifying questions
We use songs/music, playlists, users, artists, podcasts. Lets focus on finding and streaming music.

## High level metrics
- How many users do we want
    - 1 billion
- Number of songs
    - capacity for a 100 million

Let's assume that 1 song ~ 5Mb. 
- 100 million songs -> would be around 500Tb (without any replication)
    - If we do 3x replication (to prevent data loss or faster responses), we have 1.5 Pb of data

- Each song will also have some metadata like artists, date created, etc
    - 100 Bytes per song
    - 10Gb of song metadata, say worst case 100Gb. That is not a lot of data

- We might have user related data as well
    - 1Kb per user
    - 1Tb of data

- Also figure out how much data will be streaming per second
    - the interviewee did not do this

## Basic components
Let's assume that we are talking about our phone app because that is te most common way of using the application

- The application will be talking the web servers, which handle the incoming requests
- We can have multiple servers to manage load. We will have a load balancer to whcih the requests are sent. The LB decided which web server the incoming request should go to
- The web server will talk to the database to push data and get data. 

![High Level Components](/Users/arnavgupta/Desktop/High_level_components.png)

### Outline the database design
Since we have so many types of data, we will split the database. 
- songs 
- users
- metadata

Let's split into 
- song audio DB  : S3
- users, song, metadata, artists, etc DB : RDS

The web server will talk to these 2 databases. Explain why we need this differentiation
- S3 : we are storing immutable data that dont change but just need to be stored and streamed. It can also be connected to stream the data
- RDS : need to perform queries to find songs, perform joins to make playlists etc. We will also be modifying the data quite a bit (last song heard etc)

### Define the table structure of sorts
1. Songs table in RDS
- song_id
- song_url : pointing to the s3 object
- artist
- genre
- link to album cover, etc

2. S3 : will store song mp3 files

## Drill down the usecase
The usecase here is to find and stream music.
- App will request for a song name/artist name/Genre. We are basically filling out the query with details
- This is sent to the web server. Does some processing and queries the relational DB.
- relational DB returns a list of rows which fits the criteria. 
- Once the server has the list of songs, we can return this list to the user.
- The user sees a list of songs and selects one in the list. 
- CLick on the play button -> transalates into a request to stream the song and is sent to the webserver
- web server, based on the song_id, gets where the song is stored and fetch that.
- To stream the data to the user; we need a long standing connection (web socket)
    - we can either stream it chunk by chunk to the web sever -> user
    - or we can stream it directly to the web server -> then stream it to the user in smaller chunks
        - this is because the data is small enough for the web server to store the song

## Refine things as they come along/name possible issues     
- There might be songs that aren't heard as much
- There can be really popular songs having a massive load (if a popular artists releases a song). This can overload the entire path (the webserver, the song-file, bandwidth, etc)
    - A common thing to do is to use a CDN (content delivery network); like a cache to reduce the load on the backend


- This CDN will be available in different regions (very close in terms of number of hops and network connections to the user)
- If a user requests for a song in the CDN; the server doesnt do all the DB stuff, just redirects to the closest CDN
- If a song has to be moved to a CDN (based on metrics like times streamed, etc); the server tells the CDN to load the song.

- We can also have another form of caching where we can store the songs frequently played by the user/some AI based recomendations to be cached on the user's phone itself.

## Questions from the interviewer
### How would we load balance here
- the job of the load balancer is to ensure that the web server is not overloaded (often in terms of CPU).
- Since we are streaming data, instead of CPU we can look at network bandwidth in this case
    - Maybe memory for caching
    - Maybe requests outstanding

### Anything else we would refine
Since we are talking about an appliaction used everywhere, we need replication. 
Replicating the data is for
- availability and reducing downtime
- Also to keep the data closer to regions (geo-aware replication)
    - Kpop replicas and metadata closer to the APAC region

# [Design a file sharing system](https://www.youtube.com/watch?v=4_qu1F9BXow&list=PLf3F6FcQwgqEpnucyupbIqzxyvFOz9uDq&index=5)

## Clarifying questions (functional requirements)
Let's focus on downloading and uploading files, and syncing them between devices.
- Also need to have a way to notify clients that updates have occured so that it can receive the updates. 

So 
- download
- Upload
- sync between devices
- notifications

Do we have nay restrictions on the type of files?
- No, system is blind to content of the files.

What sort clients
- Desktop
- Mobile
- Web

If we upload on desktop, mobile goes ahead and downloads it. 

For a file sharing application we would ideally like to focus on
- being able to download the data anywhere
    - availability
- We should not lose data

## High level metrics
- How many users
    - 100 million signed up with 1 million daily active user

- Each user uploads 1 file a day on average

- Average file size
    - 5Mb

- Limits per user
    - Each file <= 10gb
    - Per user limit of 15Gb

- So the total data : 1.5Pb
- based on averaging we have about 11 queries per second -> say peak is double
- Throughput : 5Tb

## Draw out the high level components/design
- Clients (mobile, web, desktop) will send the requests to see files/upload files
- These requests will go to a load balancer
    - AWS elastic loadbalancer, can be custom servers
    - Serves as a layer between clients and application servers
- Application servers
    - can be EC2, or anything else is fine.

- S3 for storing the files

Let's define a block with the APIs
1. upload
    - s3 supports resumeable uploads, so we can support this as well
2. download
    - instead of getting the actual data back, we get a redirect to where the data is. the client will then go and handle the download
        - this way we keep our servers free from the load of streaming big files over the network, eating up the bandwidth.
        - s3 allows us to get temp access to an object, this way the client can securely stream the object required
3. Get file revisions (for sync)
    - Instead of getting the entire file back again;
    - send the file Id and get a list of changes made along with the timestamps
    - this can be replayed on the file via the client

back to the design element. Let's hand off some repsonsibilities to the client
- interacts with S3
- does compression
- security (auth and auth)

Why compression on client rather than API server or elsewhere?
- can put it anywhere, and logic adapts
- When we do compression on client side, we send a file that is alrady compressed, as compared to sending the normal file from client and compressing at the server
    - CPU cost to compress vs network bandwidth to consume an uncompressed file 
        - the later seems more significant, hence compression on client


## Drill down the schema 
Need to figure out what to store. Starting from the start of the flow helps
1. User
- id, user_name, email, password_hash, last_login, ...
(user exustss first, signs in which leads to username and password)
2. File
- id, path, last_version, file_hash (fast way to check diff), owner_id
    - owner's_id links back to the User's user_id
3. File versions 
- id, file_id, version_number
    - file_id links to the File table's id
4. Devices
the user is logged in on phone and desktop, one of the devices will be out of sync. maybe we can store that here
- id, user_id
    - user_id links to User's id

![Data scheme](/Users/arnavgupta/Desktop/data_schema_for_file_sharing.png)

Actual data flow happens between client and S3. Not blocking the API server with the flow.
- Keeps the interaction with the API really fast

We need to store all the tables we described above. Since there are relations between the tables we will go ahead with a relational database (RDS)

Now we need a notification service (like we had discussed in the functional requirements)
- Need a pub-sub service. let's client subscribe for notifications and application server send notification to the clients. 

- the server will send notification to the channel whenevr upload happens. The notification is consumed by the client which then acts accordingly (which will be the get in sync operation)

## Refinements
RUn through the use case and figure out what is missing. Or if we directly have an idea then just go with that directly.

- Regionality
    - this entire cell (aside from the cloud storage) can be replcated in another region if the popularity starts to rise 

    - also helps upgrade some cells without downtime (but reduced speed in that region)

- Can have CDNs to keep the data close to the users

# [Design tiktok](https://www.youtube.com/watch?v=NHqdG-aZxOk&t=3274s)

## Clarifying questions
Tiktok is generally used for a lot of things, it is a social media platform. for this interview we will restrict to 
- uploading the videos into the system
- streaming the videos

## non-functional requirements
- Number of users : 1 billion users in 150 countries
- Number of videos : 1 billion video views per day. 10 billion videos uploaded per year

Let's assume that size of 1 video : 1Mb per video

- Get total video data stored : 10^10 * 10^6 = 10^16bytes = 10Pb -> with replication around 100Pb
    - videos are generally mp4 files : lean towards blob storage
- We might have some video metadata : 1Kb per video = 10Tb of data with the same 10x replication
    - helps us find which video to show next, might not have queries across rows. need to do some aggreagtions probably : lean towards NoSQL

- Get total data streamed per second : 10^9/year -> 30 million per day -> 10Gbps ingress and 100Gbps egress (outgoing)


## High level components
Since we have good traffic for both streaming and uploading, and since they perform 2 separate tasks. We can have 2 groups of servers
1. application service (which does the streaming)
2. upload service (handles the video uploads)
which are hit via the loadbalancer. 

videos are stored in a blob store (as explained above). This is where the raw videos would go.
- can tier into hot and cold
- replicate based on regions

video metadata stuff is stored in a NoSQL (dynamoDB)
- dont need immediate consistency
- scales really well with our size which just keeps increasing
- better for aggregates

user data; this is the stuff that can have queries and relations
- amazon RDS


[QUESTION] : how can different regions and countries be reflected in our design?
- can have some sort of geo-aware load balancing; so that the request for upload goes to the closest server and database
- for streaming; we can have CDNs to store popular videos of a region close to the user. 

we will have a CDN (as discussed above)

we will also have a blackbox that decides which videos to show on the FYP. 
- once we start the app; we pick the closest 2/3 videos in the CDN. while they are being streamed; the streaming servers pick videos from the blcakbox and stream to our phone
    - everytime theere is a lag from the streaming servers/initially to do the processing, pick up videos from the CDN instead to fill the gap



# LLD
## Basic order of approach
1. ask clarifying questions
- Bounded or unbounded?
- Blocking or non-blocking on contention?
- Thread-safe or single-threaded?
- What does the error model look like?
- What are the performance constraints? 

2. Design the public facing interface
- close() is not part of cpp paradigm, it is managed by the destrcutor. Go did not have a destructor
- error model in go was to have the function signature return error (and then check if err != nil). In cpp
```cpp
// The function having the error
Value get(Key k) {
    if (!found) {
        throw std::runtime_error("key not found");
    }
    return value;
}

// the Value function's caller
try {
    Value v = get(k);
    // use v
} catch (const std::exception& e) {
    std::cerr << e.what() << "\n";   // handle the failure
}
```
3. the implementation
- the class, inheriting the intrface publicly
- the functions from the inerface need to be public
- everythign else defaults to prvate and we justify making it public.

- to initialise something once
```cpp
// these two achieve the same thing for one-time init:

// (1) call_once
std::once_flag flag;
Pool* instance = nullptr;
Pool& get_pool() {
    std::call_once(flag, []{ instance = new Pool(); });
    return *instance;
}

// (2) function-local static — simpler, same guarantee
Pool& get_pool() {
    static Pool instance;   // initialized once, thread-safely, on first call
    return instance;
}
```