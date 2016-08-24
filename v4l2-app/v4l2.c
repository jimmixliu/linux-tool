#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <malloc.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <asm/types.h>          /* for videodev2.h */
#include <linux/videodev2.h>  /* Video for Linux Two */

#define DISPLAY_SDL_ENABLE

#ifdef  DISPLAY_SDL_ENABLE
#include "SDL/SDL.h"	/*SDL*/
#include "SDL/SDL_mixer.h"
#endif


#define CAM_WIDTH 640 
#define CAM_HEIGHT 480


struct buffer {
        void *start;
        size_t length;
};

struct dev_info {
	int fd;
	int format;
        struct buffer *buffers;
	int n_buffers;
};

static char dump_enable = 0;


/*
*enumerating  menu
*/

static void enumerate_menu(struct dev_info * device, struct v4l2_queryctrl queryctrl,struct v4l2_querymenu querymenu)
{
	printf(" Menu items:\n");

	memset(&querymenu, 0, sizeof(querymenu));
	querymenu.id = queryctrl.id;

	for(querymenu.index = queryctrl.minimum; 
            querymenu.index <= queryctrl.maximum;
            querymenu.index++){
		if(0 == ioctl(device->fd,VIDIOC_QUERYMENU,&querymenu)){
			printf( "%s\n",querymenu.name);
		}
	}
}


/*
*Enumerating all user controls
*/
int query_all_controls(struct dev_info * device)
{
	struct v4l2_queryctrl queryctrl;
        struct v4l2_querymenu querymenu;

	memset(&queryctrl, 0, sizeof(queryctrl));
	memset(&querymenu, 0, sizeof(querymenu));

	for(queryctrl.id = V4L2_CID_BASE;
	    queryctrl.id < V4L2_CID_LASTP1;
	    queryctrl.id++) {
		if(0 == ioctl(device->fd, VIDIOC_QUERYCTRL, &queryctrl)) {
			if(queryctrl.flags & V4L2_CTRL_FLAG_DISABLED)
				continue;

			printf("Contrl %s\n", queryctrl.name);

			if(queryctrl.type == V4L2_CTRL_TYPE_MENU)
				enumerate_menu(device,queryctrl,querymenu);
		} else {
			if (errno == EINVAL)
				continue;

			perror("VIDIOC_QUERYCTRL");
			exit(EXIT_FAILURE);
		}
	}

	for (queryctrl.id = V4L2_CID_PRIVATE_BASE;;queryctrl.id++) {
		if (0 == ioctl(device->fd, VIDIOC_QUERYCTRL, &queryctrl)) {
			if (queryctrl.flags & V4L2_CTRL_FLAG_DISABLED)
				continue;

			printf("Control %s\n", queryctrl.name);

			if (queryctrl.type == V4L2_CTRL_TYPE_MENU)
				enumerate_menu(device,queryctrl,querymenu);
		} else {
			if (errno == EINVAL)
				break;

			perror("VIDIOC_QUERYCTRL");
			exit(EXIT_FAILURE);
		}
	}
}

/*
* Changing controls
*/

static int change_controls(struct dev_info * device)
{
	struct v4l2_queryctrl queryctrl;
	struct v4l2_control control;

	memset(&queryctrl, 0, sizeof(queryctrl));
	queryctrl.id = V4L2_CID_BRIGHTNESS;

	if (-1 == ioctl(device->fd,VIDIOC_QUERYCTRL, &queryctrl)) {
		if (errno != EINVAL) {
			perror("VIDIOC_QUERYCTRL");
			exit(EXIT_FAILURE);
		} else {
			printf("V4L2_CID_BRIGHTNESS is not supported\n");
		}
	} else if (queryctrl.flags & V4L2_CTRL_FLAG_DISABLED) {
		printf("V4L2_CID_BRIGHTNESS is not supported ");
	} else {
		memset(&control, 0, sizeof(control));
		control.id = V4L2_CID_BRIGHTNESS;
		//control.value = queryctrl.default_value;
		control.value = 0;

		printf("id = %d, type = %d, name %s\n",queryctrl.id,queryctrl.type,queryctrl.name);
		printf("minium = %d, maximum = %d\n",queryctrl.minimum,queryctrl.maximum);
		printf("step = %d, default_vaue = %d\n",queryctrl.step,queryctrl.default_value);


		if (-1 == ioctl(device->fd, VIDIOC_S_CTRL, &control)) {
			perror("VIDIOC_S_CTRL");
			exit(EXIT_FAILURE);
		}
	}
}



int init_mmap(struct dev_info * device)
{
	struct v4l2_requestbuffers req;
	struct v4l2_buffer cur_buf;
	int i,n_buffers;
	enum v4l2_buf_type type;
        
	memset(&req, 0, sizeof(req));

	/* initiate memory mapping usign IOCTL */
	/* setting the buffers count to 4 */
        req.count               = 4;
        req.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	/* specifying the memory type to MMAP */
        req.memory              = V4L2_MEMORY_MMAP;

	if (-1 == ioctl (device->fd, VIDIOC_REQBUFS, &req)) {
		printf("failed to initiate memory mapping (%d)\n", errno);
                return -1;
	}

	/* if device doesn't supprt multiple buffers */
        if (req.count < 2) {
		printf("device doesn't support multiple buffers(%d)\n",
		       req.count);
                return -1;
	}
        

	/* allocating buffers struct*/
        device->buffers = calloc (req.count, sizeof (struct buffer));
        if (!(device->buffers)) {
		printf("failed to allocate buffers\n");
		return -1;
	}

        for (n_buffers = 0; n_buffers < req.count; ++n_buffers) {
		/* using IOCTL to query the buffer status */
                memset(&cur_buf, 0, sizeof(cur_buf));
                cur_buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                cur_buf.memory      = V4L2_MEMORY_MMAP;
                cur_buf.index       = n_buffers;

                if (-1 == ioctl (device->fd, VIDIOC_QUERYBUF, &cur_buf)) {
			printf("failed to query buffer status %d\n", errno);
			return -1;
		}

		/* mmapping the buffers */
                device->buffers[n_buffers].length = cur_buf.length;
                device->buffers[n_buffers].start = 
			mmap(NULL, cur_buf.length, PROT_READ | PROT_WRITE,
			     MAP_SHARED, device->fd, cur_buf.m.offset);
                if (MAP_FAILED == device->buffers[n_buffers].start) {
			printf("failed to map buffer\n");
			return -1;
		}
        }

	/* enquiing buffers to device using IOCTL */
	for (i = 0; i < n_buffers; ++i) {
      		struct v4l2_buffer buf;

       		memset(&cur_buf, 0, sizeof(cur_buf));
       		cur_buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
       		cur_buf.memory      = V4L2_MEMORY_MMAP;
       		cur_buf.index       = i;

       		if (-1 == ioctl (device->fd, VIDIOC_QBUF, &cur_buf))
			return -1;
	}
	
	device->n_buffers = n_buffers;
	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	/* starting the streaming */
	if (-1 == ioctl (device->fd, VIDIOC_STREAMON, &type)) {
		printf("failed to start the streaming (%d)\n", errno);
		return -1;
	}

	return 1;
}

int init_video_device(struct dev_info *device)
{
	struct v4l2_capability cap;
        struct v4l2_cropcap cropcap;
        struct v4l2_crop crop;
        struct v4l2_format fmt;
	unsigned int min;

	
	/* using IOCTL to quesry the device capabilities */
        if (-1 == ioctl(device->fd, VIDIOC_QUERYCAP, &cap)) 
	{
                if (EINVAL == errno) {
                        printf ("device is no V4L2 device\n");
                        return -1;
                } else {
                        printf("Error getting device capabilities (%d)\n",
			       errno);
			return -1;
                }
        }

	/* checking if the device supports video capture */
        if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) 
	{
                printf("device is no video capture device\n");
                return -1;
        }

	/* checking if the device supports video streaming */
	if (!(cap.capabilities & V4L2_CAP_STREAMING)) 
	{
		printf("device does not support streaming i/o\n");
		return -1;
	}


        /* Using IOCTL to query the capture capabilities */
	memset(&cropcap, 0, sizeof(cropcap));
        cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (0 == ioctl (device->fd, VIDIOC_CROPCAP, &cropcap)) 
	{
                crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                crop.c = cropcap.defrect; /* reset to default */

		/* using IOCTL to set the cropping rectengle */
                if (-1 == ioctl (device->fd, VIDIOC_S_CROP, &crop)) 
		{
                        printf("failed to set cropping rectengle\n");
                }
        }


	/* setting the video data format */
        memset(&fmt, 0, sizeof(fmt));
	fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt.fmt.pix.width       = CAM_WIDTH; 
	fmt.fmt.pix.height      = CAM_HEIGHT;
	fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;

        if (-1 == ioctl (device->fd, VIDIOC_S_FMT, &fmt)) {
                printf ("Failed to set video data format\n");
		return -1;
	}

	/* initalizing device memory */
	if (init_mmap (device) < 0) {
		printf("device dosen't suppprt MMAP\n");
		return -1;
	}

   //     query_all_controls(device);
      //  change_controls(device);

	return 1;

}

static int exposure_control()
{
	
}


int dump_frame(struct buffer *buffer)
{
	int ret,fd;

	fd = open("dump_frame.yuv",O_RDWR);
	if(fd < 0){
		printf("dump_frame open failed\n");
		return -1;
	}

	ret = write(fd,buffer->start,buffer->length);
	if(ret < 0){
		printf("dump_frame write failed\n");
	}

	close(fd);
	return 0;
}

int read_frame(struct dev_info *device, void *buffer)
{
	struct v4l2_buffer buf; 
	unsigned int i;
	ssize_t read_bytes;
	unsigned int total_read_bytes;

	memset(&buf, 0, sizeof(buf));

	/* using IOCTL to dequeue a full buffer from the queue */
      	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory = V4L2_MEMORY_MMAP;

	if (-1 == ioctl(device->fd, VIDIOC_DQBUF, &buf)) {
		printf("failed to dequeue buffer (%d)\n", errno);
		return -1;
	}

	/* coping the frame data */
	memcpy(buffer, device->buffers[buf.index].start,
	       device->buffers[buf.index].length);

	if(1 == dump_enable){
		dump_frame(&device->buffers[buf.index]);
		dump_enable = 0;
	}
	

	/* using IOCTL to enqueue back the buffer after we used it */
	if (-1 == ioctl(device->fd, VIDIOC_QBUF, &buf)) {
		printf("failed to enqueue buffer (%d)\n", errno);
		return -1;
	}

	return 1;
}

int open_video_device(const char *device_name)
{
	struct stat st; 
	int fd;

        if (-1 == stat (device_name, &st)) {
                printf("stat failed\n");
                return -1;
        }

        if (!S_ISCHR(st.st_mode)) {
                printf ( "device is no char device\n");
                return -1;
        }

        fd = open(device_name, O_RDWR , 0);
        if (-1 == fd) {
                printf ( "Cannot open device\n");
                return -1;
        }
	return fd;
}

void close_video_device(struct dev_info *device)
{
	int i;
	for(i = 0; i < device->n_buffers; i++) {
		if (device->buffers) {
			munmap(device->buffers[i].start,
			       device->buffers[i].length);
		}
	}
	if (device->buffers) {
		free(device->buffers);
	}
	close(device->fd);
}

void main()
{

#ifdef DISPLAY_SDL_ENABLE
	SDL_Surface *screen = NULL;
	SDL_Overlay *data = NULL;
	SDL_Rect rect;
#endif
	struct dev_info vid_dev;
	int ret, frames_count = 0;

	memset(&vid_dev, 0, sizeof(vid_dev));
	/* initializing video device - use your own device instead */
	vid_dev.fd = open_video_device("/dev/video0");
	if (vid_dev.fd < 0) {
		printf("failed to open video device\n");
		return;
	}
	ret = init_video_device(&vid_dev);
	if (ret < 0) {
		printf("failed to initalize video device\n");
		goto close_video;
	}

#ifdef DISPLAY_SDL_ENABLE
	/* initializing SDL */
	if(SDL_Init(SDL_INIT_VIDEO)) {
  		printf("Could not initialize SDL - %s\n", SDL_GetError());
		goto close_video;
	}

	/* Set up the screen */
	screen = SDL_SetVideoMode(CAM_WIDTH, CAM_HEIGHT, 0,
				SDL_ANYFORMAT | SDL_DOUBLEBUF | SDL_RESIZABLE);

	/* If there was in error in setting up the screen */
	if(screen == NULL)
	{
		printf("failed to set up screen\n");
		goto close_sdl;
	}

	/* Set the window caption */
	SDL_WM_SetCaption("www.Linux-Programmer-Guide.com V4L2 example", NULL);
	/* Setting YUV OVERLAY */
	data = SDL_CreateYUVOverlay(CAM_WIDTH, CAM_HEIGHT, SDL_YUY2_OVERLAY,
				    screen);
	rect.x = 0;
	rect.y = 0;
	rect.w = CAM_WIDTH;
	rect.h = CAM_HEIGHT;
#endif
	/* capturing 500 frames (abouth 20 seconds) */
	/* each frame data is copied to the YUV OVERLAY */
	while (frames_count++ < 500) {	
		if(frames_count == 450)
			dump_enable = 1;
#ifdef DISPLAY_SDL_ENABLE
		SDL_LockYUVOverlay(data);
#endif
		read_frame(&vid_dev, data->pixels[0]);

#ifdef DISPLAY_SDL_ENABLE
		SDL_UnlockYUVOverlay(data);
		/* displaying the YUV data on the screen */
		SDL_DisplayYUVOverlay(data, &rect);
#endif
	}
close_sdl:
#ifdef DISPLAY_SDL_ENABLE
	SDL_Quit();
#endif
close_video:
	close_video_device(&vid_dev);

}
