ACLOCAL_AMFLAGS = -I m4
bin_PROGRAMS = qstream_client
qstream_client_SOURCES = controller.cpp main.cpp output.cpp trackerconnector.cpp ../common/data.cpp ../common/log.cpp ../common/MemPipe.cpp ../common/msg_ack.cpp ../common/msg_data.cpp ../common/msg_spread.cpp ../common/msg_tree.cpp ../common/os_functions.cpp ../common/socket_functions_lin.cpp ../common/stringtools.cpp ../common/symmatrix.cpp ../common/tcpstack.cpp ../common/uppermatrix.cpp
qstream_client_LDADD = 
AM_CXXFLAGS = $(BOOST_CPPFLAGS) -DLINUX 
AM_CFLAGS = 
AM_LDFLAGS = $(BOOST_LDFLAGS) $(BOOST_THREAD_LIB) -ldl