OBJS = asyncdns.o blowfish.o config.o connection.o d_chat.o filedb.o license.o \
       listen.o m_chat.o m_file.o m_keydist.o m_lcaps.o m_ping.o m_search.o \
       m_upload.o mqueue.o mqueuelist.o netkern.o sha.o sharedmain.o shbuf.o \
       srchwnd.o srvmain.o util.o xfers.o xferwnd.o 

RSA_OBJS = rsa/md5.o rsa/nn.o rsa/prime.o rsa/r_keygen.o rsa/r_random.o \
	   rsa/rsa.o

DEPLOY_FLAGS = -s -O2 -DNODEBUG -DNDEBUG -march=pentiumpro
DEVELOP_FLAGS = -g -O0 -DDEBUG -march=pentiumpro
WARNING_FLAGS = -Wall -Wno-trigraphs
INCLUDES = -I. -Irsa
DEFINES = -D_DEFINE_SRV 

STYLE_FLAGS = $(DEPLOY_FLAGS)
#STYLE_FLAGS = $(DEVELOP_FLAGS)

CFLAGS = -pipe $(STYLE_FLAGS) $(WARNING_FLAGS) $(INCLUDES) $(DEFINES)
CXXFLAGS = $(CFLAGS)

default: wastesrv

wastesrv: $(OBJS) $(RSA_OBJS)
	$(CXX) $(DEBUGFLAG) -o wastesrv -pthread $(OBJS) $(RSA_OBJS)

clean:
	-rm *.o rsa/*.o wastesrv
