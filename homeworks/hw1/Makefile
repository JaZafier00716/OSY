TARGET1 = main1/main1
STATICNAME = static_lib
STATICLIB = $(STATICNAME)/lib$(STATICNAME).a
STATICLIBOBJ = $(STATICNAME)/$(STATICNAME).o


TARGET2 = main2/main2
DYNNAME1 = dyn_lib1
DYNNAME2 = dyn_lib2
DYN_LIB1 = $(DYNNAME1)/libcompute.so
DYN_LIB2 = $(DYNNAME2)/libcompute.so
DYN_LIBS = $(DYN_LIB1) $(DYN_LIB2)
DYN_LIB1OBJ = $(DYNNAME1)/compute.o
DYN_LIB2OBJ = $(DYNNAME2)/compute.o


CFLAGS += -g -Wall -Werror
CPPFLAGS += -I$(STATICNAME)
STATIC_LDFLAGS = -L$(STATICNAME)
STATIC_LDLIBS = -l$(STATICNAME)


all: $(TARGET1) $(TARGET2) $(DYN_LIBS)

# clean binary files
clean:
	rm -f $(TARGET1) $(TARGET2) $(STATICLIB) $(STATICLIBOBJ) $(DYN_LIB1) $(DYN_LIB2) $(DYN_LIB1OBJ) $(DYN_LIB2OBJ) \
		$(DYNNAME1)/lib$(DYNNAME1).so $(DYNNAME2)/lib$(DYNNAME2).so

# creating static library
$(STATICLIB): $(STATICLIBOBJ)
	@echo "\nCreating static library...\n"
	ar rcs $@ $^
	@echo "\nStatic library created: '$(STATICLIB)'.\n"

$(STATICLIBOBJ): $(STATICNAME)/$(STATICNAME).c $(STATICNAME)/$(STATICNAME).h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

# creating static-library application
$(TARGET1): $(TARGET1).c $(STATICLIB)
	@echo "\nCreating application...\n"
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STATIC_LDFLAGS) $< $(STATIC_LDLIBS) -o $@
	@echo "\nApplication '$(TARGET1)' is ready.\n"

# creating dynamic libraries
$(DYN_LIB1): dyn_lib1/compute.c dyn_lib1/compute.h
	@echo "\nCreating dynamic library '$(DYN_LIB1)'...\n"
	$(CC) $(CFLAGS) -fPIC -shared $< -o $@

$(DYN_LIB2): dyn_lib2/compute.c dyn_lib2/compute.h
	@echo "\nCreating dynamic library '$(DYN_LIB2)'...\n"
	$(CC) $(CFLAGS) -Wno-unused-variable -fPIC -shared $< -o $@

# creating dynamic-library application
$(TARGET2): $(TARGET2).c $(DYN_LIB1)
	@echo "\nCreating application...\n"
	$(CC) $(CPPFLAGS) -I$(DYNNAME1) $(CFLAGS) -L$(DYNNAME1) $< -lcompute -o $@
	@echo "\nApplication '$(TARGET2)' is ready.\n"