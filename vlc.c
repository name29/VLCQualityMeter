#include <stdio.h>
#include <stdlib.h>
#include <vlc/vlc.h>
#include <unistd.h>
#include <time.h>
#include <inttypes.h>
#include <string.h>

#define MAX_LOG_LINE_LENGTH 10240

void onVlcBuffering(const libvlc_event_t* event, void* userData);
void onLogCallback(void* data, int level, const libvlc_log_t *log, const char *fmt, va_list ap);

int main(int argc, char* argv[])
{
	char* state2string[]={ "NothingSpecial","Opening","Buffering","Playing","Paused","Stopped","Ended","Error"};

	FILE* fp;

	int last_displayed=0;
	int last_check_time=0;
	float fps=0;

	libvlc_instance_t * inst;
	libvlc_media_player_t *mp;
	libvlc_media_t *m;
	/* Load the VLC engine */
	inst = libvlc_new (0, NULL);

	fp=fopen("vlc.log","w");
	if ( fp == NULL )
	{
		return 1;
	}
	fclose(fp);

	fp=fopen("report.csv","w");
	if ( fp == NULL )
	{
		return 1;
	}
	fclose(fp);

	libvlc_log_set(inst,onLogCallback,NULL);
	/* Create a new item */
	m = libvlc_media_new_location (inst, argv[1]);

	/* Create a media player playing environement */
	mp = libvlc_media_player_new_from_media (m);

	libvlc_event_manager_t* eMan = libvlc_media_player_event_manager(mp);
	libvlc_event_attach(eMan,libvlc_MediaPlayerBuffering,onVlcBuffering,NULL);


	/* play the media_player */
	libvlc_media_player_play (mp);

	while (1)
	{
		libvlc_media_stats_t stats;
		libvlc_state_t state_media;
		libvlc_state_t state_player;
		state_media=libvlc_media_get_state(m);
		state_player=libvlc_media_player_get_state(mp);

		int err = libvlc_media_get_stats(m,&stats);
		if (err)
		{
			int now_time=time(NULL);

			if (now_time == last_check_time) continue;

			if(last_check_time != 0 )
			{
				fps=stats.i_displayed_pictures-last_displayed;
				fps=fps/(now_time-last_check_time);
			}

			FILE* fp=fopen("report.csv","a");
			if ( fp == NULL )
			{
				printf("Unable to open report.csv\n");
				break;
			}

			fprintf(fp,"%d;",now_time);
			fprintf(fp,"STATUS;");
			fprintf(fp,"%d;",stats.i_lost_pictures);
			fprintf(fp,"%d;",stats.i_displayed_pictures);
			fprintf(fp,"%f;",fps);
			fprintf(fp,"%s;",state2string[state_media]);
			fprintf(fp,"%s;\n",state2string[state_player]);

			fclose(fp);

			printf("Lost %d\n",stats.i_lost_pictures);
			printf("Displayed %d\n",stats.i_displayed_pictures);
			printf("FPS %f\n",fps);
			printf("M state %s\n",state2string[state_media]);
			printf("MP state %s\n",state2string[state_player]);

			last_check_time=time(NULL);
			last_displayed=stats.i_displayed_pictures;
		}
		if (state_player == 6 ||
			state_player == 7 ||
			state_media == 6 ||
			state_media == 7 )
		{
			break;
		}
		sleep(2);
	}

	/* Stop playing */
	libvlc_media_player_stop (mp);

	/* Free the media_player */
	libvlc_media_player_release (mp);

	/* No need to keep the media now */
	libvlc_media_release (m);

	libvlc_release (inst);

	return 0;
}


int64_t getMilliseconds()
{
	struct timespec t;
	clock_gettime(CLOCK_REALTIME,&t);
	return t.tv_sec*INT64_C(1000)+t.tv_nsec/1000000;
}

void onVlcBuffering(const libvlc_event_t* event, void* userData)
{
	float percent = (float) event->u.media_player_buffering.new_cache;

	FILE* fp=fopen("report.csv","a");
	if ( fp == NULL )
	{
		printf("Unable to open report.csv\n");
		exit(1);
	}

	fprintf(fp,"%"PRId64";",getMilliseconds());
	fprintf(fp,"BUFFERING;");
	fprintf(fp,"%f%%;\n",percent);
	fclose(fp);
}


void reportEvent(char * tag ,char* msg)
{
	FILE* fp=fopen("report.csv","a");
	if ( fp == NULL )
	{
		printf("Unable to open report.csv\n");
		exit(1);
	}

	fprintf(fp,"%"PRId64";",getMilliseconds());
	fprintf(fp,"%s;",tag);
	fprintf(fp,"%s;\n",msg);
	fclose(fp);
}

void onLogCallback(void* data, int level, const libvlc_log_t *log,
			const char *fmt, va_list ap)
{
	FILE* fp=fopen("vlc.log","a");
	if ( fp == NULL )
	{
		printf("Unable to open vlc.log\n");
		exit(1);
	}

	const char* name;
	const char* header;
	char buffer[10240];

	libvlc_log_get_object(log,&name,&header,NULL);
	vsprintf(buffer,fmt,ap);
	fprintf(fp,"%"PRId64";",getMilliseconds());
	fprintf(fp,"Level %d Module '%s' Header '%s' %s\n",level,name,header,buffer);
	fclose(fp);

	if(strcmp(name,"audio output") == 0 )
	{
		if( strstr(buffer,"deferring start") != NULL ) reportEvent("WARNING_EVENT",buffer);
		else if( strstr(buffer,"starting late") != NULL ) reportEvent("WARNING_EVENT",buffer);
		else if( strstr(buffer,"underflow") != NULL ) reportEvent("ERROR_EVENT",buffer);
		else if( strstr(buffer,"playback too early") != NULL ) reportEvent("WARNING_EVENT",buffer);
		else if( strstr(buffer,"playback way too early") != NULL ) reportEvent("ERROR_EVENT",buffer);
	}
	else if ( strcmp(name,"video output") == 0 )
	{
		if( strstr(buffer,"picture is too late to be displayed") != NULL ) reportEvent("ERROR_EVENT",buffer);
	}
	else if ( strcmp(name,"decoder") == 0 )
	{
		if( strstr(buffer,"More than") != NULL ) reportEvent("ERROR_EVENT",buffer);
	}
	else if ( strcmp(name,"input") == 0 )
	{
		if( strstr(buffer,"ES_OUT_SET_(GROUP_)PCR is caled too late") != NULL ) reportEvent("ERROR_EVENT",buffer);
	}
	else if ( strcmp(name,"stream") == 0 )
	{
		if( strstr(buffer,"playback in danger of stalling") != NULL ) reportEvent("ERROR_EVENT",buffer);
		else if ( strstr(buffer,"predicted to take") != NULL ) reportEvent("ERROR_EVENT",buffer);
	}
}