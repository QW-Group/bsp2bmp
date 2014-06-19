/*

bsp2bmp - converts Quake I BSP's to a bitmap (map!) of the level
Copyright (C) 1999  Matthew Wong <cot@freeshell.org>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/


#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <math.h>
#include <values.h>
#include <stdint.h>

#define PROGNAME  "bsp2bmp"
#define V_MAJOR   0
#define V_MINOR   0
#define V_REV     15
#define V_SUBREV  "d"

#define Z_PAD_HACK    16
#define MAX_REF_FACES 4

#ifndef MAXINT
    #include <limits.h>
    #define MAXINT INT_MAX

#endif

/*
Thanks fly to Id for a hackable game! :)

Types are copied from the BSP section of the Quake specs,
thanks to Olivier Montanuy et al.
*/
	
int	 quiet;

/* Data structs */
typedef struct dentry_t {
	long		offset;
	long		size;
} dentry_t;

typedef struct dheader_t {
	long version;
	dentry_t	entities;
	dentry_t	planes;

	dentry_t	miptex;
	dentry_t	vertices;

	dentry_t	visilist;
	dentry_t	nodes;

	dentry_t	texinfo;
	dentry_t	faces;

	dentry_t	lightmaps;
	dentry_t	clipnodes;

	dentry_t	leaves;

	dentry_t	iface;
	dentry_t	edges;

	dentry_t	ledges;
	dentry_t	models;
} dheader_t;

/* vertices */
typedef struct vertex_t {
	float		X;
	float		Y;
	float		Z;
} vertex_t;

/* edges */
typedef struct edge_t {
	unsigned short	vertex0; /* index of start vertex, 0..numvertices */
	unsigned short	vertex1; /* index of   end vertex, 0..numvertices */
} edge_t;

/* faces */
typedef struct face_t {
	unsigned short	plane_id;

	unsigned short	side;
	long		ledge_id;

	unsigned short	ledge_num;
	unsigned short	texinfo_id;
    
	unsigned char	typelight;
	unsigned char	baselight;
	unsigned char	light[2];
	long		lightmap;
} face_t;

/* MW */
typedef struct edge_extra_t {
	long		num_face_ref;
	long		ref_faces[MAX_REF_FACES]; /* which faces ref'd */
	vertex_t	ref_faces_normal[MAX_REF_FACES]; /* normal of faces */
	int		ref_faces_area[MAX_REF_FACES]; /* area of faces */
} edge_extra_t;

typedef unsigned char eightbit;

typedef struct options_t {
	char	*bspf_name;
	char	*outf_name;

	float	 scaledown;
	float	 z_pad;

	float	 image_pad;

	int	 z_direction;
	int	 camera_axis;
	/* 1 - X, 2 - Y, 3 - Z, negatives come from negative side of axis */

	int	 edgeremove;
	float	 flat_threshold;
	int	 area_threshold;
	int	 linelen_threshold;

	int	 negative_image;

	int	 write_raw;
	int	 write_nocomp;
} options_t;

typedef struct bmp_infoheader_t {
	long		headersize;
	long		imagewidth;
	long		imageheight;
	unsigned short	planes;
	unsigned short	bitcount;
	long		compression;
	long		datasize;
	long		xpelspermeter;
	long		ypelspermeter;
	long		colsused;
	long		colsimportant;
} bmp_infoheader_t;

typedef struct bmp_fileheader_t {
	eightbit	filetype[2];
	long		filesize;    /* 4     : 4 */
	unsigned short	unused1;     /* 2 x 2 : 4 */
	unsigned short	unused2;     /* 2 x 2 : 4 */
	long		data_ofs;    /* 4     : 4 */
} bmp_fileheader_t;

typedef struct rgb_quad_t {
	eightbit	red;
	eightbit	green;
	eightbit	blue;
	eightbit	unused;
} rgb_quad_t;

/*---------------------------------------------------------------------------*/

void stdprintf( char *fmt, ... ) {
	va_list argp;
	if (!quiet)
	{
		va_start( argp, fmt );
		vfprintf( stdout, fmt, argp );
		va_end( argp );
	}
}

/*---------------------------------------------------------------------------*/

void show_help() {
	stdprintf("BSP->bitmap, version %d.%d.%d%s\n",V_MAJOR,V_MINOR,V_REV,V_SUBREV);
	stdprintf("Copyright (c) 1999-2004, Matthew Wong\n\n");
	stdprintf("Usage:\n");
	stdprintf("  %s [options] <bspfile> [outfile]\n\n",PROGNAME);
	stdprintf("Options:\n");
	stdprintf("    -s<scaledown>     default: 4, ie 1/4 scale\n");
	stdprintf("    -z<z_scaling>     default: 0 for flat map, >0 for iso 3d, -1 for auto\n");
	stdprintf("    -p<padding>       default: 16-pixel border around final image\n");
	stdprintf("    -d<direction>     iso 3d direction: 7  0  1\n");
	stdprintf("                                         \\ | /\n");
	stdprintf("                                        6--+--2\n");
	stdprintf("                                         / | \\\n");
	stdprintf("                                        5  4  3\n");
	stdprintf("                      default: 7\n");
	stdprintf("    -c<camera_axis>   default: +Z (+/- X/Y/Z axis)\n");
	stdprintf("    -t<flatness>      threshold of dot product for edge removal;\n");
	stdprintf("                      default is 0.90\n");
	stdprintf("    -e                disable extraneous edges removal\n");
	stdprintf("    -a<area>          minimum area for a polygon to be drawn\n");
	stdprintf("                      default is 0\n");
	stdprintf("    -l<length>        minimum length for an edge to be drawn\n");
	stdprintf("                      default is 0\n");
	stdprintf("    -n                negative image (black on white\n");
	stdprintf("    -r                write raw data, rather than bmp file\n");
	stdprintf("    -q                quiet output\n");
	// stdprintf("    -u                write uncompressed bmp\n");
	stdprintf("\n");
	stdprintf("If [outfile] is omitted, then program will create .bmp file in the same directory as .bsp file.\n");
	return;
}

/*---------------------------------------------------------------------------*/

void plotpoint(eightbit *image, long width, long height, long xco, long yco, unsigned int color) {
	unsigned int bigcol=0;

	if(xco < 0 || xco > width || yco < 0 || yco > height)
		return;

	bigcol=(unsigned int)image[yco * width + xco];

	bigcol=bigcol + color;

	if (bigcol < 0)
		bigcol=0;
	if (bigcol > 255)
		bigcol=255;

	image[yco * width + xco]=(eightbit)bigcol;

	return;
}

/*---------------------------------------------------------------------------*/

void bresline(eightbit *image, long width, long height, long x1, long y1, long x2, long y2, unsigned int color) {
	long x=0, y=0;
	long deltax=0, deltay=0;
	long xchange=0, ychange=0;
	long error, length, i;

	x=x1;
	y=y1;

	deltax=x2 - x1;
	deltay=y2 - y1;

	if (deltax < 0) {
		xchange = -1;
		deltax  = -deltax;
	} else {
		xchange = 1;
	}

	if (deltay < 0) {
		ychange = -1;
		deltay  = -deltay;
	} else {
		ychange = 1;
	}

	/* Main seq */
	error = 0;
	i = 0;

	if (deltax < deltay) {
		length = deltay + 1;
		while (i < length) {
			y = y + ychange;
			error = error + deltax;
			if (error > deltay) {
				x = x + xchange;
				error = error - deltay;
			}
	        	i++;

			plotpoint(image, width, height, x, y, color);
		}
	} else {
		length = deltax + 1;
		while ( i < length) {
			x = x + xchange;
			error = error + deltay;
			if (error > deltax) {
				y = y + ychange;
				error = error - deltax;
			}
	        	i++;

			plotpoint(image, width, height, x, y, color);
		}
	}
}

/*---------------------------------------------------------------------------*/

void def_options(struct options_t *opt) {
	static struct options_t locopt;

	locopt.bspf_name = NULL;
	locopt.outf_name = NULL;

	locopt.scaledown = 4.0;
	locopt.image_pad = 16.0;
	locopt.z_pad     = 0.0;

	locopt.z_direction = 1;
	locopt.camera_axis = 3; /* default is from +Z */

	locopt.edgeremove = 1;
	locopt.flat_threshold = 0.90;
	locopt.area_threshold = 0;
	locopt.linelen_threshold = 0;

	locopt.negative_image = 0;

	locopt.write_raw = 0;
	locopt.write_nocomp = 1;

	memcpy(opt, &locopt, sizeof(struct options_t));
	return;
}

/*---------------------------------------------------------------------------*/

void get_options(struct options_t *opt, int argc, char *argv[]) {
	static struct options_t	 locopt;
	int			 i=0;
	char			*arg;
	long			 lnum=0;
	float			 fnum=0.0;
	char			 pm='+', axis='Z';

	/* Man I hate parsing options... */

	/* Copy curr options */
	memcpy(&locopt, opt, sizeof(struct options_t));

	/* Go through command line */
	for (i=1; i<argc; i++) {
		arg=argv[i];
		if(arg[0] == '-') {
			/* Okay, dash-something */
			switch(arg[1]) {
				case 'q':
					quiet = 1;
					break;
				
				case 's':
					if(sscanf(&arg[2],"%ld",&lnum) == 1)
						if (lnum > 0)
							locopt.scaledown = (float)lnum;
					break;

				case 'z':
					if(sscanf(&arg[2],"%ld",&lnum) == 1)
						if (lnum >= -1)
							locopt.z_pad = (float)lnum;
					break;

				case 'p':
					if(sscanf(&arg[2],"%ld",&lnum) == 1)
						if (lnum >= 0)
							locopt.image_pad = (float)lnum;
					break;
				
				case 'd':
					if(sscanf(&arg[2],"%ld",&lnum) == 1)
						if (lnum >= 0 && lnum <= 7)
							locopt.z_direction = (int)lnum;
					break;
				
				case 'c':
					if(strlen(&arg[2]) == 2) {
						pm = arg[2];
						axis = arg[3];
						stdprintf("-c%c%c\n",pm,axis);
						switch(axis) {
							case 'x':
							case 'X':
								locopt.camera_axis=1;
								break;
							
							case 'y':
							case 'Y':
								locopt.camera_axis=2;
								break;
							
							case 'z':
							case 'Z':
								locopt.camera_axis=3;
								break;
							
							default:
								stdprintf("Must specify a valid axis.\n");
								show_help();
								exit(1);
								break;
						}

						switch(pm) {
							case '+':
								break;
							case '-':
								locopt.camera_axis=-locopt.camera_axis;
								break;
							default:
								stdprintf("Must specify +/-\n");
								show_help();
								exit(1);
								break;
						}
					} else {
						stdprintf("Unknown option: -%s\n",&arg[1]);
						show_help();
						exit(1);
					}
					break;
				case 't':
					if(sscanf(&arg[2],"%f",&fnum) == 1)
						if (fnum >= 0.0 && fnum <= 1.0)
							locopt.flat_threshold = (float)fnum;
					break;
				
				case 'e':
					locopt.edgeremove = 0;
					break;
				
				case 'n':
					locopt.negative_image = 1;
					break;
				
				case 'a':
					if(sscanf(&arg[2],"%ld",&lnum) == 1)
						if (lnum >= 0)
							locopt.area_threshold = (int)lnum;
					break;
				
				case 'l':
					if(sscanf(&arg[2],"%ld",&lnum) == 1)
						if (lnum >= 0)
							locopt.linelen_threshold = (int)lnum;
					break;
				
				case 'r':
					locopt.write_raw = 1;
					break;
				
				case 'u':
					locopt.write_nocomp = 1;
					break;
				
				default:
					stdprintf("Unknown option: -%s\n",&arg[1]);
					show_help();
					exit (1);
					break;
			} /* switch */
		} else {
			if(locopt.bspf_name == NULL) {
				locopt.bspf_name = arg;
			} else if (locopt.outf_name == NULL) {
				locopt.outf_name = arg;
			} else {
				stdprintf("Unknown option: %s\n",arg);
				show_help();
				exit(1);
			}
		} /* if */
	} /* for */

	memcpy(opt, &locopt, sizeof(struct options_t));
	return;
}

/*---------------------------------------------------------------------------*/

void show_options(struct options_t *opt)
  {
	char   dirstr[80];

	stdprintf("Options:\n");
	stdprintf("  Scale down by: %.0f\n",opt->scaledown);
	stdprintf("  Z scale: %.0f\n",opt->z_pad);
	stdprintf("  Border: %.0f\n",opt->image_pad);
	/* Zoffset calculations */
	switch (opt->z_direction) {
		case 0:
			sprintf(dirstr,"up");
			break;
		case 1:
			sprintf(dirstr,"up & right");
			break;
		case 2:
			sprintf(dirstr,"right");
			break;
		case 3:
			sprintf(dirstr,"down & right");
			break;
		case 4:
			sprintf(dirstr,"down");
			break;
		case 5:
			sprintf(dirstr,"down & left");
			break;
		case 6:
			sprintf(dirstr,"left");
			break;
		case 7:
			sprintf(dirstr,"up & left");
			break;
		default:
			sprintf(dirstr,"unknown!");
			break;
	}

	stdprintf("  Z direction: %d [%s]\n",opt->z_direction,dirstr);
	if (opt->z_pad == 0) {
		stdprintf("    Warning: direction option has no effect with Z scale set to 0.\n");
	}

	/* Camera axis */
	switch (opt->camera_axis) {
		case 1:
			sprintf(dirstr,"+X");
			break;
		case -1:
			sprintf(dirstr,"-X");
			break;
		case 2:
			sprintf(dirstr,"+Y");
			break;
		case -2:
			sprintf(dirstr,"-Y");
			break;
		case 3:
			sprintf(dirstr,"+Z");
			break;
		case -3:
			sprintf(dirstr,"-Z");
			break;
		default:
			sprintf(dirstr,"unknown!");
			break;
	}

	stdprintf("  Camera axis: %s\n", dirstr);
	stdprintf("  Remove extraneous edges: %s\n", (opt->edgeremove == 1) ? "yes" : "no");
	stdprintf("  Edge removal dot product theshold: %f\n", opt->flat_threshold);
	stdprintf("  Minimum polygon area threshold (approximate): %d\n", opt->area_threshold);
	stdprintf("  Minimum line length threshold: %d\n", opt->linelen_threshold);
	stdprintf("  Creating %s image.\n", (opt->negative_image == 1) ? "negative" : "positive");

	stdprintf("\n");
	stdprintf("  Input (bsp) file: %s\n",opt->bspf_name);
	if(opt->write_raw)
		stdprintf("  Output (raw) file: %s\n\n",opt->outf_name);
	else
		stdprintf("  Output (%s bmp) file: %s\n\n",opt->write_nocomp ? "uncompressed" : "RLE compressed" ,opt->outf_name);

	return;
}

/*===========================================================================*/

int main(int argc, char *argv[]) {
	FILE                 *bspfile=NULL;
	FILE                 *outfile=NULL;
	long                  i=0, j=0, k=0, x=0;
	long                  pad=0x00000000;
	struct dheader_t      bsp_header;

	struct vertex_t      *vertexlist=NULL;
	struct edge_t        *edgelist=NULL;
	struct face_t        *facelist=NULL;
	int                  *ledges=NULL;

	/* edge removal stuff */
	struct edge_extra_t  *edge_extra=NULL;
	struct vertex_t       v0, v1, vect;
	int                   area, usearea;

	long                  numedges=0;
	long                  numlistedges=0;
	long                  numvertices=0;
	long                  numfaces=0;

	float                 minX=0.0, maxX=0.0, minY=0.0, maxY=0.0, minZ=0.0, maxZ=0.0, midZ=0.0, tempf=0.0;
	long                  Zoffset0=0, Zoffset1=0;
	long                  Z_Xdir=1, Z_Ydir=-1;

	long                  imagewidth=0,imageheight=0;

	eightbit             *image;
	struct options_t      options;
	int                   drawcol;

	static struct bmp_fileheader_t  bmpfileheader;
	static struct bmp_infoheader_t  bmpinfoheader;
	static struct rgb_quad_t        rgbquad;

	/*****/


	/* Enough args? */
	if (argc < 2) {
		show_help();
		return 1;
	}

	/* Setup options */
	def_options(&options);
	get_options(&options,argc,argv);
	show_options(&options);
    /* Create Output file name if it is not provided */
    if(options.outf_name ==  NULL)
    {
        options.outf_name=malloc(strlen(options.bspf_name)+1);
        strncpy (options.outf_name, options.bspf_name,strlen(options.bspf_name)-3);
        strcat(options.outf_name,"bmp");
        fprintf(stdout,"Assuming BMP name from BSP name: %s\n",options.outf_name);
    }

	bspfile=fopen(options.bspf_name,"rb");
	if (bspfile == NULL) {
		fprintf(stderr,"Error opening bsp file %s.\n",options.bspf_name);
		return 1;
	}

	/* Read header */
	stdprintf("Reading header...");
	/* OLD:
	i = fread(&bsp_header, sizeof(struct dheader_t), 1, bspfile);
	*/
	i = 0;
	i = i + fread(&bsp_header.version, sizeof(long), 1, bspfile);
	/* entities: */
	i = i + fread(&bsp_header.entities.offset, sizeof(long), 1, bspfile);
	i = i + fread(&bsp_header.entities.size, sizeof(long), 1, bspfile);
	/* planes: */
	i = i + fread(&bsp_header.planes.offset, sizeof(long), 1, bspfile);
	i = i + fread(&bsp_header.planes.size, sizeof(long), 1, bspfile);
	/* miptex: */
	i = i + fread(&bsp_header.miptex.offset, sizeof(long), 1, bspfile);
	i = i + fread(&bsp_header.miptex.size, sizeof(long), 1, bspfile);
	/* vertices: */
	i = i + fread(&bsp_header.vertices.offset, sizeof(long), 1, bspfile);
	i = i + fread(&bsp_header.vertices.size, sizeof(long), 1, bspfile);
	/* visilist: */
	i = i + fread(&bsp_header.visilist.offset, sizeof(long), 1, bspfile);
	i = i + fread(&bsp_header.visilist.size, sizeof(long), 1, bspfile);
	/* nodes: */
	i = i + fread(&bsp_header.nodes.offset, sizeof(long), 1, bspfile);
	i = i + fread(&bsp_header.nodes.size, sizeof(long), 1, bspfile);
	/* texinfo: */
	i = i + fread(&bsp_header.texinfo.offset, sizeof(long), 1, bspfile);
	i = i + fread(&bsp_header.texinfo.size, sizeof(long), 1, bspfile);
	/* faces: */
	i = i + fread(&bsp_header.faces.offset, sizeof(long), 1, bspfile);
	i = i + fread(&bsp_header.faces.size, sizeof(long), 1, bspfile);
	/* lightmaps: */
	i = i + fread(&bsp_header.lightmaps.offset, sizeof(long), 1, bspfile);
	i = i + fread(&bsp_header.lightmaps.size, sizeof(long), 1, bspfile);
	/* clipnodes: */
	i = i + fread(&bsp_header.clipnodes.offset, sizeof(long), 1, bspfile);
	i = i + fread(&bsp_header.clipnodes.size, sizeof(long), 1, bspfile);
	/* leaves: */
	i = i + fread(&bsp_header.leaves.offset, sizeof(long), 1, bspfile);
	i = i + fread(&bsp_header.leaves.size, sizeof(long), 1, bspfile);
	/* iface: */
	i = i + fread(&bsp_header.iface.offset, sizeof(long), 1, bspfile);
	i = i + fread(&bsp_header.iface.size, sizeof(long), 1, bspfile);
	/* edges: */
	i = i + fread(&bsp_header.edges.offset, sizeof(long), 1, bspfile);
	i = i + fread(&bsp_header.edges.size, sizeof(long), 1, bspfile);
	/* ledges: */
	i = i + fread(&bsp_header.ledges.offset, sizeof(long), 1, bspfile);
	i = i + fread(&bsp_header.ledges.size, sizeof(long), 1, bspfile);
	/* models: */
	i = i + fread(&bsp_header.models.offset, sizeof(long), 1, bspfile);
	i = i + fread(&bsp_header.models.size, sizeof(long), 1, bspfile);

	if (i != 31) {
		stdprintf("error %s!\n",strerror(errno));
		return 1;
	} else {
		stdprintf("done.\n");
	}

	numvertices = (bsp_header.vertices.size/sizeof(struct vertex_t));
	numedges = (bsp_header.edges.size/sizeof(struct edge_t));
	numlistedges = (bsp_header.ledges.size/sizeof(short));
	numfaces = (bsp_header.faces.size/sizeof(struct face_t));

	/* display header */
	stdprintf("Header info:\n\n");
	stdprintf(" version %ld\n",bsp_header.version);
	stdprintf(" vertices - offset %ld\n",bsp_header.vertices.offset);
	stdprintf("          - size %ld",bsp_header.vertices.size);
	stdprintf(" [numvertices = %ld]\n", numvertices);
	stdprintf("\n");

	stdprintf("    edges - offset %ld\n",bsp_header.edges.offset);
	stdprintf("          - size %ld",bsp_header.edges.size);
	stdprintf(" [numedges = %ld]\n", numedges);
	stdprintf("\n");

	stdprintf("   ledges - offset %ld\n",bsp_header.ledges.offset);
	stdprintf("          - size %ld",bsp_header.ledges.size);
	stdprintf(" [numledges = %ld]\n", numlistedges);
	stdprintf("\n");

	stdprintf("    faces - offset %ld\n",bsp_header.faces.offset);
	stdprintf("          - size %ld",bsp_header.faces.size);
	stdprintf(" [numfaces = %ld]\n", numfaces);
	stdprintf("\n");

	/* Read vertices -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -   */
	vertexlist = malloc(sizeof(struct vertex_t) * numvertices);
	if (vertexlist == NULL) {
		fprintf(stderr,"Error allocating %ld bytes for vertices.",sizeof(struct vertex_t) * numvertices);
		return 2;
	}

	stdprintf("Reading %ld vertices...",numvertices);
	if (fseek(bspfile,bsp_header.vertices.offset,SEEK_SET)) {
		fprintf(stderr, "error seeking to %ld\n",bsp_header.vertices.offset);
		return 1;
	} else {
		stdprintf("seek to %ld...",ftell(bspfile));
	}
	/* OLD:
	i=fread(vertexlist, sizeof(struct vertex_t), numvertices, bspfile);
	*/
	i = 0;
	while (i < numvertices) {
		j = 0;
		j = j + fread(&(vertexlist[i].X), sizeof(float), 1, bspfile);
		j = j + fread(&(vertexlist[i].Y), sizeof(float), 1, bspfile);
		j = j + fread(&(vertexlist[i].Z), sizeof(float), 1, bspfile);
		if (j < 3)
			break;
		i++;
	}
	if (i != numvertices) {
		fprintf(stderr, "error %s! only %ld read.\n",strerror(errno),i);
		return 1;
	} else {
		stdprintf("successfully read %ld vertices.\n",i);
	}

	/* Read edges -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -   */
	edgelist = malloc(sizeof(struct edge_t) * numedges);
	if (edgelist == NULL) {
		fprintf(stderr,"Error allocating %ld bytes for edges.",sizeof(struct edge_t) * numedges);
		return 2;
	}

	stdprintf( "Reading %ld edges...",numedges);
	if (fseek(bspfile,bsp_header.edges.offset,SEEK_SET)) {
		fprintf(stderr,"error seeking to %ld\n",bsp_header.vertices.offset);
		return 1;
	} else {
		stdprintf("seek to %ld...",ftell(bspfile));
	}
	/* OLD:
	i=fread(edgelist, sizeof(struct edge_t), numedges, bspfile);
	*/
	i = 0;
	while (i < numedges) {
		j = 0;
		j = j + fread(&(edgelist[i].vertex0), sizeof(unsigned short), 1, bspfile);
		j = j + fread(&(edgelist[i].vertex1), sizeof(unsigned short), 1, bspfile);
		if (j < 2)
			break;
		i++;
	}
	if (i != numedges) {
		fprintf(stderr, "error %s! only %ld read.\n",strerror(errno),i);
		return 1;
	} else {
		stdprintf("successfully read %ld edges.\n",i);
	}

	/* Read ledges   -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -   */
	ledges = malloc(sizeof(short) * numlistedges);
	if (ledges == NULL) {
		fprintf(stderr,"Error allocating %ld bytes for ledges.",sizeof(short) * numlistedges);
		return 2;
	}

	stdprintf("Reading ledges...");
	if (fseek(bspfile,bsp_header.ledges.offset,SEEK_SET)) {
		fprintf(stderr, "error seeking to %ld\n",bsp_header.ledges.offset);
		return 1;
	} else {
		stdprintf("seek to %ld...",ftell(bspfile));
	}
	i=fread(ledges,sizeof(short),numlistedges,bspfile);
	if (i != numlistedges) {
		fprintf(stderr, "error %s! only %ld read.\n",strerror(errno),i);
		return 1;
	} else {
		stdprintf("successfully read %ld ledges.\n",i);
	}

	/* Read faces -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -   */
	facelist = malloc(sizeof(struct face_t) * numfaces);
	if (facelist == NULL) {
		fprintf(stderr,"Error allocating %ld bytes for faces.",sizeof(short) * numfaces);
		return 2;
	}

	stdprintf("Reading faces...");
	if (fseek(bspfile,bsp_header.faces.offset,SEEK_SET)) {
		fprintf(stderr, "error seeking to %ld\n",bsp_header.faces.offset);
		return 1;
	} else {
		stdprintf("seek to %ld...",ftell(bspfile));
	}
	/* OLD:
	i=fread(facelist,sizeof(struct face_t),numfaces,bspfile);
	*/
	i = 0;
	while (i < numfaces) {
		j = 0;
		j = j + fread(&(facelist[i].plane_id),sizeof(unsigned short), 1, bspfile);
		j = j + fread(&(facelist[i].side),sizeof(unsigned short), 1, bspfile);
		j = j + fread(&(facelist[i].ledge_id),sizeof(long), 1, bspfile);
		j = j + fread(&(facelist[i].ledge_num),sizeof(unsigned short), 1, bspfile);
		j = j + fread(&(facelist[i].texinfo_id),sizeof(unsigned short), 1, bspfile);
		j = j + fread(&(facelist[i].typelight),sizeof(unsigned char), 1, bspfile);
		j = j + fread(&(facelist[i].baselight),sizeof(unsigned char), 1, bspfile);
		j = j + fread(&(facelist[i].light[0]),sizeof(unsigned char), 1, bspfile);
		j = j + fread(&(facelist[i].light[1]),sizeof(unsigned char), 1, bspfile);
		j = j + fread(&(facelist[i].lightmap),sizeof(long), 1, bspfile);
		if (j < 10)
			break;
		i++;
	}
	if (i != numfaces) {
		fprintf(stderr, "error %s! only %ld read.\n",strerror(errno),i);
		return 1;
	} else {
		stdprintf("successfully read %ld faces.\n",i);
	}

	/* Should be done reading stuff -  -  -  -  -  -  -  -  -  -  -  -   */
	fclose(bspfile);

	/* Precalc stuff if we're removing edges -  -  -  -  -  -  -  -  -   */
	/*
	typedef struct edge_extra_t
	  {
	    int       num_face_ref;
	    int       ref_faces[MAX_REF_FACES];
	    vertex_t  ref_faces_normal[MAX_REF_FACES];
	  } edge_extra_t;
	*/

	if (options.edgeremove) {
		stdprintf("Precalc edge removal stuff...\n");
		edge_extra = malloc(sizeof(struct edge_extra_t) * numedges);
		if (edge_extra == NULL) {
			fprintf(stderr,"Error allocating %ld bytes for extra edge info.",sizeof(struct edge_extra_t) * numedges);
			return 2;
		}
		
		/* initialize the array */
		for (i=0; i<numedges; i++) {
			edge_extra[i].num_face_ref=0;
			for (j=0;j<MAX_REF_FACES;j++) {
				edge_extra[i].ref_faces[j]=-1;
			}
		} 
		
		for (i=0; i<numfaces; i++) {
			/* calculate the normal (cross product) */
			/*   starting edge: edgelist[ledges[facelist[i].ledge_id]] */
			/* number of edges: facelist[i].ledge_num; */

			/* quick hack - just take the first 2 edges */
			j=facelist[i].ledge_id;
			k=j;
			vect.X = 0.0; vect.Y = 0.0; vect.Z = 0.0;
			while (vect.X == 0.0 && vect.Y == 0.0 && vect.Z == 0.0 && k < (facelist[i].ledge_num + j)) {
				/* If the first 2 are par?llel edges, go with the next one */
				k++;
				/*
				if (i == (numfaces-1))
				k=0;
				*/

				if (ledges[j] > 0) {
					v0.X = vertexlist[edgelist[abs((int)ledges[j])].vertex0].X - vertexlist[edgelist[abs((int)ledges[j])].vertex1].X;
					v0.Y = vertexlist[edgelist[abs((int)ledges[j])].vertex0].Y - vertexlist[edgelist[abs((int)ledges[j])].vertex1].Y;
					v0.Z = vertexlist[edgelist[abs((int)ledges[j])].vertex0].Z - vertexlist[edgelist[abs((int)ledges[j])].vertex1].Z; 
		
					v1.X = vertexlist[edgelist[abs((int)ledges[k])].vertex0].X - vertexlist[edgelist[abs((int)ledges[k])].vertex1].X;
					v1.Y = vertexlist[edgelist[abs((int)ledges[k])].vertex0].Y - vertexlist[edgelist[abs((int)ledges[k])].vertex1].Y;
					v1.Z = vertexlist[edgelist[abs((int)ledges[k])].vertex0].Z - vertexlist[edgelist[abs((int)ledges[k])].vertex1].Z;
				} else {
					/* negative index, therefore walk in reverse order */
					v0.X = vertexlist[edgelist[abs((int)ledges[j])].vertex1].X - vertexlist[edgelist[abs((int)ledges[j])].vertex0].X;
					v0.Y = vertexlist[edgelist[abs((int)ledges[j])].vertex1].Y - vertexlist[edgelist[abs((int)ledges[j])].vertex0].Y;
					v0.Z = vertexlist[edgelist[abs((int)ledges[j])].vertex1].Z - vertexlist[edgelist[abs((int)ledges[j])].vertex0].Z;

					v1.X = vertexlist[edgelist[abs((int)ledges[k])].vertex1].X - vertexlist[edgelist[abs((int)ledges[k])].vertex0].X;
					v1.Y = vertexlist[edgelist[abs((int)ledges[k])].vertex1].Y - vertexlist[edgelist[abs((int)ledges[k])].vertex0].Y;
					v1.Z = vertexlist[edgelist[abs((int)ledges[k])].vertex1].Z - vertexlist[edgelist[abs((int)ledges[k])].vertex0].Z;
				}
			
				/* cross product */
				vect.X = (v0.Y * v1.Z) - (v0.Z * v1.Y);
				vect.Y = (v0.Z * v1.X) - (v0.X * v1.Z);
				vect.Z = (v0.X * v1.Y) - (v0.Y * v1.X);

				/* Okay, it's not the REAL area, but i'm lazy, and since a lot of mapmakers use rectangles anyways... */
				area = (int)(sqrt(v0.X*v0.X + v0.Y*v0.Y + v0.Z*v0.Z) * sqrt(v1.X*v1.X + v1.Y*v1.Y + v1.Z*v1.Z));
			} /* while */
			
			/* reduce cross product to a unit vector */
			tempf = (float)sqrt((double)(vect.X*vect.X + vect.Y*vect.Y + vect.Z*vect.Z));
			//stdprintf("%4ld - (%8.3f, %8.3f, %8.3f) X (%8.3f, %8.3f, %8.3f) = (%8.3f, %8.3f, %8.3f) -> ",i,v0.X, v0.Y, v0.Z, v1.X, v1.Y, v1.Z, vect.X, vect.Y, vect.Z);
			if (tempf > 0.0) {
				vect.X = vect.X / tempf;
				vect.Y = vect.Y / tempf;
				vect.Z = vect.Z / tempf;
			} else {
				vect.X = 0.0;
				vect.Y = 0.0;
				vect.Z = 0.0;
			}
			//stdprintf("(%8.3f, %8.3f, %8.3f)\n",vect.X, vect.Y, vect.Z);
			/* Now go put ref in all edges... */
			// stdprintf("<id=%ld|num=%ld>",facelist[i].ledge_id, facelist[i].ledge_num);
			for (j=0; j<facelist[i].ledge_num; j++) {
				k = j + facelist[i].ledge_id;
				x = edge_extra[abs((int)ledges[k])].num_face_ref;
				//stdprintf("e%d(le%ld)",abs((int)ledges[k]),k);
				if (edge_extra[abs((int)ledges[k])].num_face_ref < MAX_REF_FACES) {
					x++;
					edge_extra[abs((int)ledges[k])].num_face_ref = x;
					edge_extra[abs((int)ledges[k])].ref_faces[x-1]=i;
					edge_extra[abs((int)ledges[k])].ref_faces_normal[x-1].X=vect.X;
					edge_extra[abs((int)ledges[k])].ref_faces_normal[x-1].Y=vect.Y;
					edge_extra[abs((int)ledges[k])].ref_faces_normal[x-1].Z=vect.Z;
					edge_extra[abs((int)ledges[k])].ref_faces_area[x-1]=area;
				}
			} /* for */
		}
	}

	/* . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . */

	stdprintf("Collecting min/max\n");
	/* Collect min and max */
	for (i=0;i<numvertices;i++) {
		/* DEBUG - print vertices */
		// stdprintf("vertex %ld: (%f, %f, %f)\n", i, vertexlist[i].X, vertexlist[i].Y, vertexlist[i].Z);

		
		/* Ugly hack - flip stuff around for different camera angles */
		switch(options.camera_axis) {
			case -1:
				/* -X -- (-y <-->  +y, +x into screen, -x out of screen; -z down, +z up) */
				tempf = vertexlist[i].X;
				vertexlist[i].X = vertexlist[i].Y;
				vertexlist[i].Y = vertexlist[i].Z;
				vertexlist[i].Z = -tempf;
				break;
			case 1:
				/* +X -- (+y <--> -y; -x into screen, +x out of screen; -z down, +z up) */
				tempf = vertexlist[i].X;
				vertexlist[i].X = -vertexlist[i].Y;
				vertexlist[i].Y =  vertexlist[i].Z;
				vertexlist[i].Z =  tempf;
				break;
			case -2:
				/* -Y -- (+x <--> -x; -y out of screen, +z up) */
				vertexlist[i].X = -vertexlist[i].X;
				tempf = vertexlist[i].Z;
				vertexlist[i].Z = vertexlist[i].Y;
				vertexlist[i].Y =  tempf;
				break;
			case 2:
				/* +Y -- (-x <--> +x; +y out of screen, +z up) */
				tempf = vertexlist[i].Z;
				vertexlist[i].Z = -vertexlist[i].Y;
				vertexlist[i].Y =  tempf;
				break;
			case -3:
				/* -Z -- negate X and Z (ie. 180 rotate along Y axis) */
				vertexlist[i].X = -vertexlist[i].X;
				vertexlist[i].Z = -vertexlist[i].Z;
				break;
			case 3:	/* +Z -- do nothing! */
			default:/* do nothing! */
				break;
		} /* switch */
		
		/* flip Y for proper screen cords */
		vertexlist[i].Y = -vertexlist[i].Y;
		
		/* max and min */
		if (i == 0) {
			minX = vertexlist[i].X;
			maxX = vertexlist[i].X;
			
			minY = vertexlist[i].Y;
			maxY = vertexlist[i].Y;
			
			minZ = vertexlist[i].Z;
			maxZ = vertexlist[i].Z;
		} else {
			if (vertexlist[i].X < minX)
				minX=vertexlist[i].X;
			if (vertexlist[i].X > maxX)
				maxX=vertexlist[i].X;

			if (vertexlist[i].Y < minY)
				minY=vertexlist[i].Y;
			if (vertexlist[i].Y > maxY)
				maxY=vertexlist[i].Y;

			if (vertexlist[i].Z < minZ)
				minZ=vertexlist[i].Z;
			if (vertexlist[i].Z > maxZ)
				maxZ=vertexlist[i].Z;
		}
	}

minX = minY = minZ = -4096;
maxX = maxY = maxZ = 4096;
	
	if (options.z_pad == -1)
		options.z_pad = (long)(maxZ - minZ) / (options.scaledown * Z_PAD_HACK);

	midZ=(maxZ + minZ) / 2.0;
	stdprintf("\n");
	stdprintf("Bounds: X [%8.4f .. %8.4f] delta: %8.4f\n",minX,maxX,(maxX-minX));
	stdprintf("        Y [%8.4f .. %8.4f] delta: %8.4f\n",minY,maxY,(maxY-minY));
	stdprintf("        Z [%8.4f .. %8.4f] delta: %8.4f - mid: %8.4f\n",minZ,maxZ,(maxZ-minZ),midZ);

	/* image array */
	imagewidth  = (long)((maxX - minX)/options.scaledown) + (options.image_pad*2) + (options.z_pad*2);
	imageheight = (long)((maxY - minY)/options.scaledown) + (options.image_pad*2) + (options.z_pad*2);
	if(!(image=malloc(sizeof(eightbit) * imagewidth * imageheight))) {
		fprintf(stderr,"Error allocating image buffer %ldx%ld.\n",imagewidth,imageheight);
		return 0;
	} else {
		stdprintf("Allocated buffer %ldx%ld for image.\n",imagewidth,imageheight);
		memset(image,0,sizeof(eightbit) * imagewidth * imageheight);
	}

	/* Zoffset calculations */
	switch (options.z_direction) {
		case 0:
			Z_Xdir=0;  /* up */
			Z_Ydir=1;
			break;
		
		case 1:
			Z_Xdir=1;  /* up & right */
			Z_Ydir=-1;
			break;
		
		case 2:
			Z_Xdir=1;  /* right */
			Z_Ydir=0;
			break;
		
		case 3:
			Z_Xdir=1;  /* down & right */
			Z_Ydir=1;
			break;
		
		case 4:
			Z_Xdir=0;  /* down */
			Z_Ydir=1;
			break;
		
		case 5:
			Z_Xdir=-1; /* down & left */
			Z_Ydir=1;
			break;
		
		case 6:
			Z_Xdir=-1; /* left */
			Z_Ydir=0;
			break;
		
		case 7:
			Z_Xdir=-1; /* up & left */
			Z_Ydir=-1;
			break;
		
		default:
			Z_Xdir=1;  /* unknown - go with case 1 */
			Z_Ydir=-1;
			break;
	}

	/* Plot edges on image */
	stdprintf("Plotting edges...");
	k=0;
	drawcol=(options.edgeremove) ? 64 : 32;
	for(i=0;i<numedges;i++) {
		/*

		fprintf(stderr, "Edge %ld: vertex %d (%f, %f, %f) -> %d (%f, %f, %f)\n",
		i,
		edgelist[i].vertex0,
		vertexlist[edgelist[i].vertex0].X,
		vertexlist[edgelist[i].vertex0].Y,
		vertexlist[edgelist[i].vertex0].Z,
		edgelist[i].vertex1,
		vertexlist[edgelist[i].vertex1].X,
		vertexlist[edgelist[i].vertex1].Y,
		vertexlist[edgelist[i].vertex1].Z);
		*/
		
		/* Do a check on this line ... keep this line or not */
		// fprintf(stderr,"edge %ld is referenced by %ld faces\n",i,edge_extra[i].num_face_ref);

		/* run through all referenced faces */
		/* ICK ... do I want to check area of all faces? */

	  	usearea = MAXINT;
		if (options.edgeremove) {
			// fprintf(stderr,"Edge %ld - ref=%ld",i,edge_extra[i].num_face_ref);
			if (edge_extra[i].num_face_ref > 1) {
				tempf = 1.0;
				/* dot products of all referenced faces */
				for (j=0; j<edge_extra[i].num_face_ref - 1; j=j+2) {
					/* dot product */
					/*
					fprintf(stderr,". (%8.3f,%8.3f,%8.3f) . (%8.3f,%8.3f,%8.3f)",
					edge_extra[i].ref_faces_normal[j].X, edge_extra[i].ref_faces_normal[j].Y,
					edge_extra[i].ref_faces_normal[j].Z, edge_extra[i].ref_faces_normal[j+1].X,
					edge_extra[i].ref_faces_normal[j+1].Y, edge_extra[i].ref_faces_normal[j+1].Z);
					*/

					tempf = tempf * (edge_extra[i].ref_faces_normal[j].X * edge_extra[i].ref_faces_normal[j+1].X +
							 edge_extra[i].ref_faces_normal[j].Y * edge_extra[i].ref_faces_normal[j+1].Y +
							 edge_extra[i].ref_faces_normal[j].Z * edge_extra[i].ref_faces_normal[j+1].Z);
	              
					/* What is the smallest area this edge references? */
					if (usearea > edge_extra[i].ref_faces_area[j])
						usearea = edge_extra[i].ref_faces_area[j];
					if (usearea > edge_extra[i].ref_faces_area[j+1])
						usearea = edge_extra[i].ref_faces_area[j+1];
				} /* for */
			} else {
				tempf = 0.0;
			}
			// fprintf(stderr," = %8.3f\n",tempf);
		} else {
			tempf = 0.0;
		}
		
		if ((abs(tempf) < options.flat_threshold) &&
		    (usearea > options.area_threshold) &&
		    (sqrt((vertexlist[edgelist[i].vertex0].X - vertexlist[edgelist[i].vertex1].X) *
		    (vertexlist[edgelist[i].vertex0].X - vertexlist[edgelist[i].vertex1].X) +
		    (vertexlist[edgelist[i].vertex0].Y - vertexlist[edgelist[i].vertex1].Y) *
		    (vertexlist[edgelist[i].vertex0].Y - vertexlist[edgelist[i].vertex1].Y) +
		    (vertexlist[edgelist[i].vertex0].Z - vertexlist[edgelist[i].vertex1].Z) *
		    (vertexlist[edgelist[i].vertex0].Z - vertexlist[edgelist[i].vertex1].Z)) > options.linelen_threshold)) {
			Zoffset0=(long)(options.z_pad * (vertexlist[edgelist[i].vertex0].Z - midZ) / (maxZ - minZ));
			Zoffset1=(long)(options.z_pad * (vertexlist[edgelist[i].vertex1].Z - midZ) / (maxZ - minZ));
			
			bresline(image, imagewidth, imageheight,
			         (long)((vertexlist[edgelist[i].vertex0].X - minX)/options.scaledown + options.image_pad + options.z_pad + (float)(Zoffset0 * Z_Xdir)),
				 (long)((vertexlist[edgelist[i].vertex0].Y - minY)/options.scaledown + options.image_pad + options.z_pad + (float)(Zoffset0 * Z_Ydir)),
				 (long)((vertexlist[edgelist[i].vertex1].X - minX)/options.scaledown + options.image_pad + options.z_pad + (float)(Zoffset1 * Z_Xdir)),
				 (long)((vertexlist[edgelist[i].vertex1].Y - minY)/options.scaledown + options.image_pad + options.z_pad + (float)(Zoffset1 * Z_Ydir)),
				 drawcol);
		} else {
			k++;
		}
	} /* for numedges */

	stdprintf("%ld edges plotted",numedges);
	if(options.edgeremove) {
		stdprintf(" (%ld edges removed)\n",k);
	} else {
		stdprintf("\n");
	}

  /*
	// Little gradient
	for (i=0;i<=255;i++) {
		plotpoint(image,imagewidth,imageheight,i,0,(255-i)); // across from top left
		plotpoint(image,imagewidth,imageheight,0,i,(255-i)); // down from top left
		plotpoint(image,imagewidth,imageheight,(imagewidth-i-1),0,(255-i)); // back from top right
		plotpoint(image,imagewidth,imageheight,(imagewidth-1),i,(255-i)); // down from top right

		plotpoint(image,imagewidth,imageheight,(imagewidth-i-1),(imageheight-1),(255-i)); // back from bottom right
		plotpoint(image,imagewidth,imageheight,(imagewidth-1),(imageheight-i-1),(255-i)); // up from bottom right

		plotpoint(image,imagewidth,imageheight,i,(imageheight-1),(255-i)); // across from bottom left
		plotpoint(image,imagewidth,imageheight,0,(imageheight-i-1),(255-i)); // up from bottom left
	}
  */

	/* Negate image if necessary */
	if (options.negative_image) {
		for (i=0;i<imageheight;i++) {
			for (j=0;j < imagewidth; j++) {
				image[i * imagewidth + j] = 255 - image[i * imagewidth + j];
			}
		}
	}

	/* Write image */
	outfile=fopen(options.outf_name,"wb");
	if (outfile == NULL) {
		fprintf(stderr,"Error opening output file %s.\n",options.outf_name);
		return 1;
	}

	if (options.write_raw) {
		i=fwrite(image,sizeof(eightbit) * imagewidth * imageheight, 1,outfile);
		if (i != 1) {
			fprintf(stderr,"Error writing raw data to %s\n",options.outf_name);
			return 1;
		}
	} else {
		/* Silly header - 54-byte header */
		/* (14 fileheader, 40 infoheader), 1024-byte palette */

		bmpfileheader.filetype[0]=(eightbit)0x42;
		bmpfileheader.filetype[1]=(eightbit)0x4d;
		bmpfileheader.filesize=(long)sizeof(eightbit) * (long)imagewidth * (long)imageheight + (long)1024 + (long)54;
		bmpfileheader.unused1=(unsigned short)0x0000;
		bmpfileheader.unused2=(unsigned short)0x0000;
		bmpfileheader.data_ofs=(long)(1024 + 54);
		
		// stdprintf("bfh.fs=%ld\n",bmpfileheader.filesize);
		// stdprintf("sizesum = %d\n",sizeof(unsigned short) + sizeof(long) + sizeof(unsigned short) + sizeof(unsigned short) + sizeof(long));

		bmpinfoheader.headersize=(long)40; /* 0x28 */
		bmpinfoheader.imagewidth=(long)imagewidth;
		bmpinfoheader.imageheight=(long)imageheight;
		bmpinfoheader.planes=(unsigned short)01;
		bmpinfoheader.bitcount=(unsigned short)8; /* 8-bits, 256-color image */
		bmpinfoheader.compression=(long)0x00000000; /* No compression */
		//bmpinfoheader.datasize=(long)(sizeof(eightbit) * imagewidth * imageheight); /* Could put 0, since its valid for uncompressed image */
		bmpinfoheader.datasize=(long)0x00000000;
		bmpinfoheader.xpelspermeter=(long)0x00006338; /* Arbitrary 100dpi */
		bmpinfoheader.ypelspermeter=(long)0x00006338;
		bmpinfoheader.xpelspermeter=(long)0x00000b6d; /* ImageMagick value :) */
		bmpinfoheader.ypelspermeter=(long)0x00000b6d;
		bmpinfoheader.colsused=(long)0x00000100; /* 256 colors */
		bmpinfoheader.colsimportant=(long)0x00000100;
		// stdprintf("sizeof(bfh_t) = %d\n",sizeof(bmp_fileheader_t));
		// stdprintf("sizeof(bih_t) = %d\n",sizeof(bmp_infoheader_t));

		/* OLD:
		i=fwrite(&filetype,sizeof(unsigned short),1,outfile);
		*/
		i = 0;
		i = i + fwrite(&(bmpfileheader.filetype[0]), sizeof(eightbit),1,outfile);
		i = i + fwrite(&(bmpfileheader.filetype[1]), sizeof(eightbit),1,outfile);
		i = i + fwrite(&bmpfileheader.filesize, sizeof(long),1,outfile);
		i = i + fwrite(&bmpfileheader.unused1, sizeof(unsigned short),1,outfile);
		i = i + fwrite(&bmpfileheader.unused2, sizeof(unsigned short),1,outfile);
		i = i + fwrite(&bmpfileheader.data_ofs, sizeof(long),1,outfile);
		if (i != 6) {
			fprintf(stderr,"Error writing bmp file header.\n");
			return 1;
		}

		/* OLD:
		i=fwrite(&bmpinfoheader,sizeof(bmp_infoheader_t),1,outfile);
		*/
		i = 0;
		i = i + fwrite(&bmpinfoheader.headersize,sizeof(long),1,outfile);
		i = i + fwrite(&bmpinfoheader.imagewidth,sizeof(long),1,outfile);
		i = i + fwrite(&bmpinfoheader.imageheight,sizeof(long),1,outfile);
		i = i + fwrite(&bmpinfoheader.planes,sizeof(unsigned short),1,outfile);
		i = i + fwrite(&bmpinfoheader.bitcount,sizeof(unsigned short),1,outfile);
		i = i + fwrite(&bmpinfoheader.compression,sizeof(long),1,outfile);
		i = i + fwrite(&bmpinfoheader.datasize,sizeof(long),1,outfile);
		i = i + fwrite(&bmpinfoheader.xpelspermeter,sizeof(long),1,outfile);
		i = i + fwrite(&bmpinfoheader.ypelspermeter,sizeof(long),1,outfile);
		i = i + fwrite(&bmpinfoheader.colsused,sizeof(long),1,outfile);
		i = i + fwrite(&bmpinfoheader.colsimportant,sizeof(long),1,outfile);
		if (i != 11) {
			fprintf(stderr,"Error writing bmp info header.\n");
			return 1;
		}

		/* Write the palette */
		for(j=0; j<256; j++) {
			rgbquad.red    = (eightbit)j;
			rgbquad.green  = (eightbit)j;
			rgbquad.blue   = (eightbit)j;
			rgbquad.unused = (eightbit)0x00;
			i = 0;
			i = i + fwrite(&rgbquad.red,sizeof(eightbit),1,outfile);
			i = i + fwrite(&rgbquad.green,sizeof(eightbit),1,outfile);
			i = i + fwrite(&rgbquad.blue,sizeof(eightbit),1,outfile);
			i = i + fwrite(&rgbquad.unused,sizeof(eightbit),1,outfile);
			if (i != 4) {
				fprintf(stderr,"Error writing RGB Palette.\n");
				return 1;
			}
		}

		/* Data */
		/* EVIL - BMP files are inverted */
		// if (options.write_nocomp)
		if (1) {
			/* K is the amount to pad by */
			k = sizeof(long) - ((sizeof(eightbit) * imagewidth) % sizeof(long));
			k = (k == sizeof(long)) ? 0 : k;
			for (j=1; j<=imageheight; j++) {
				// Set of data: image[(int)((imageheight-j)*imagewidth)];  size = sizeof(eightbit) * (imagewidth)
				i=fwrite(&image[(int)((imageheight-j)*imagewidth)], sizeof(eightbit) * (imagewidth), 1, outfile);
				if (i != 1) {
					fprintf(stderr,"Error writing bmp data to %s at line %ld\n",options.outf_name,j);
					return 1;
				}

				if (k > 0) {
					if(fwrite(&pad,k,1,outfile) != 1) {
						fprintf(stderr,"Error writing bmp data padding to %s at line %ld\n",options.outf_name,j);
						return 1;
					}
				}
			} /* for */
		} else {
			/* TODO: Write compressed file? */
		}
	} 
	
	stdprintf("File written to %s.\n",options.outf_name);
	fclose(outfile);

	/* Close, done! */
	free(vertexlist);
	free(edgelist);
	free(ledges);
	free(facelist);
	free(image);
	if (options.edgeremove) {
		free(edge_extra);
	}

	if (options.write_raw) {
		stdprintf("\nIf you want to (and have ImageMagick's convert):\n  convert -verbose -colors 256 -size %ldx%ld gray:%s map.jpg\n",imagewidth,imageheight,options.outf_name);
	} else {
		stdprintf("\nIf you want to (and have ImageMagick's convert):\n  convert -verbose -colors 256 bmp:%s map.jpg\n\n",options.outf_name);
	}

	return 0;
}
