struct file {
	enum { FD_NONE, FD_PIPE, FD_INODE } type;
	int ref; // reference count (koliko puta je otvorena)
	char readable; // da li moze da se cita
	char writable;	// da li moze da se pise
	struct pipe *pipe; // opis pipe-a
	struct inode *ip; // opis datoteke
	uint off;
};


// in-memory copy of an inode
struct inode { //* opis fajla
	uint dev;           // Device number
	uint inum;          // Inode number
	int ref;            // Reference count
	struct sleeplock lock; // protects everything below here
	int valid;          // inode has been read from disk?

	short type;         // copy of disk inode
	short major;
	short minor;
	short nlink;
	uint size;
	uint addrs[NDIRECT+1];
};

// table mapping major device number to
// device functions
struct devsw {
	int (*read)(struct inode*, char*, int); // pokazivaci na funkciju
	int (*write)(struct inode*, char*, int);
};

extern struct devsw devsw[]; // niz struktura devsw

#define CONSOLE 1
// ovde define-ovati druge uredjaje ako ih ima