//
// Copyright (c) 2014 Félix Cloutier
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#include "XboxOneController.h"

OSDefineMetaClassAndStructors(com_felixcloutier_driver_XboxOneControllerDriver, IOUSBHIDDriver)

#if DEBUG
// Do not use this macro with numbered arguments.
# define IO_LOG_DEBUG(fmt, ...) IOLog("%s: " fmt "\n", __func__, ## __VA_ARGS__)
#else
# define IO_LOG_DEBUG(...)
#endif

// The convention is to #define 'super' to the superclass's name, Java-style, and then use that
// through the code.
#define super IOUSBHIDDriver

// "com_felixcloutier_driver_XboxOneController" is a heck of a long name. This should make it prettier.
#define XboxOneControllerDriver com_felixcloutier_driver_XboxOneControllerDriver

// Magic words to make the controller work.
constexpr UInt8 XboxOneControllerHelloMessage[] = {0x05, 0x20};

// Report descriptor. I made it myself.
// This guy (http://eleccelerator.com/tutorial-about-usb-hid-report-descriptors/) has a nice tutorial.
// See http://www.usb.org/developers/devclass_docs/Hut1_12v2.pdf for usage page and usage.
// See http://www.usb.org/developers/hidpage#HID%20Descriptor%20Tool for Windows tool to create/parse HID report
// descriptors.
constexpr UInt8 XboxOneControllerReportDescriptor[] = {
	0x05, 0x01,					// USAGE_PAGE (Generic Desktop)
	0x09, 0x05,					// USAGE (Game pad)
	0xa1, 0x01,					// COLLECTION (Application)
		0xa1, 0x00,				// COLLECTION (Physical)
	
			// 20 00 ss EC (where ss is a sequence number)
			0x09, 0x3f,			// USAGE (Reserved)
			0x75, 0x20,			// REPORT_SIZE (16)
			0x95, 0x01,			// REPORT_COUNT (1)
			0x81, 0x02,			// INPUT (Data,Var,Abs)
	
			// buttons
			0x05, 0x09,			// USAGE_PAGE (Button)
			0x19, 0x01,			// USAGE_MINIMUM (Button 1)
			0x29, 0x10,			// USAGE_MAXIMUM (Button 16)
			0x15, 0x00,			// LOGICAL_MINIMUM (0)
			0x25, 0x01,			// LOGICAL_MAXIMUM (1)
			0x95, 0x10,			// REPORT_COUNT (16)
			0x75, 0x01,			// REPORT_SIZE (1)
			0x81, 0x02,			// INPUT (Data,Var,Abs)
	
			// triggers
			// Colin Munro's Xbox 360 controller driver uses Z and Rz instead of buttons.
			// OS X seems to dislike non-boolean buttons, so that's what I'll be doing too.
			0x05, 0x01,			// USAGE_PAGE (Generic Desktop)
			0x09, 0x32,			// USAGE (Z)
			0x09, 0x35,			// USAGE (Rz)
			0x15, 0x00,			// LOGICAL_MINIMUM (0)
			0x26, 0x00, 0x04,	// LOGICAL_MAXIMUM (1024)
			0x75, 0x10,			// REPORT_SIZE (16)
			0x95, 0x02,			// REPORT_COUNT (2)
			0x81, 0x02,			// INPUT (Data,Var,Abs)
	
			// hat prefixes
			0x16, 0x00, 0x80,	// LOGICAL_MINIMUM (-32768)
			0x26, 0xff, 0x7f,	// LOGICAL_MAXIMUM (32767)
			0x36, 0x00, 0x80,	// PHYSICAL MINIMUM (-32768)
			0x46, 0xff, 0x7f,	// PHYSICAL_MAXIMUM (32767)
			0x95, 0x02,			// REPORT_COUNT (2)
			0x75, 0x10,			// REPORT_SIZE (16)
			0x05, 0x01,			// USAGE_PAGE (Generic Desktop)
	
			// left hat
			0x09, 0x01,			// USAGE (Pointer)
			0xa1, 0x00,			// COLLECTION (Physical)
				0x09, 0x30,		// USAGE (X)
				0x09, 0x31,		// USAGE (Y)
				0x81, 0x02,		// INPUT (Data,Var,Abs)
			0xc0,				// END COLLECTION
	
			// right hat
			0x09, 0x01,			// USAGE (Pointer)
			0xa1, 0x00,			// COLLECTION (Physical)
				0x09, 0x33,		// USAGE (Rx)
				0x09, 0x34,		// USAGE (Ry)
				0x81, 0x02,		// INPUT (Data,Var,Abs)
			0xc0,				// END COLLECTION
	
		0xc0,					// END COLLECTION
	0xc0,						// END COLLECTION
};

IOReturn XboxOneControllerDriver::newReportDescriptor(IOMemoryDescriptor **descriptor) const
{
	if (descriptor == nullptr)
	{
		return kIOReturnBadArgument;
	}
	
	IOMemoryDescriptor* buffer = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task, 0, sizeof XboxOneControllerReportDescriptor);
	if (buffer == nullptr)
	{
		return kIOReturnNoMemory;
	}
	
	IOByteCount written = buffer->writeBytes(0, XboxOneControllerReportDescriptor, sizeof XboxOneControllerReportDescriptor);
	if (written != sizeof XboxOneControllerReportDescriptor) // paranoid check
	{
		return kIOReturnNoSpace;
	}
	
	*descriptor = buffer;
	return kIOReturnSuccess;
}

bool XboxOneControllerDriver::handleStart(IOService *provider)
{
	// Apple says to call super::handleStart at the *beginning* of the method.
	if (!super::handleStart(provider))
	{
		return false;
	}
	
	// Is it the correct kind of object?
	IOUSBInterface* interface = OSDynamicCast(IOUSBInterface, provider);
	if (interface == nullptr)
	{
		IO_LOG_DEBUG("IOUSBHIDDriver is handling start on an object that's not a IOUSBInterface??");
		return false;
	}
	
	// Create the hello message that we're about to send to the controller.
	IOMemoryDescriptor* hello = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task, 0, 2);
	if (hello == nullptr)
	{
		IO_LOG_DEBUG("Could not allocate buffer for hello message.");
		return false;
	}
	hello->writeBytes(0, XboxOneControllerHelloMessage, sizeof XboxOneControllerHelloMessage);
	
	IOReturn ior = kIOReturnError;
	// Find the pipe to which we have to send the hello message.
	IOUSBFindEndpointRequest pipeRequest = {
		.type = kUSBInterrupt,
		.direction = kUSBOut,
	};
	
	pipeRequest.direction = kUSBOut;
	IOUSBPipe* pipeToController = interface->FindNextPipe(nullptr, &pipeRequest);
	if (pipeToController != nullptr)
	{
		// Everything's in order now. Tell the controller that it can start working.
		ior = pipeToController->Write(hello, 0, 0, hello->getLength());
		if (ior != kIOReturnSuccess)
		{
			IO_LOG_DEBUG("Couldn't send hello message to controller: %08x\n", ior);
		}
	}
	
	// Well, that's all for initialization. Thanks folks!
	hello->release();
	return ior == kIOReturnSuccess;
}

IOReturn XboxOneControllerDriver::handleReport(IOMemoryDescriptor *descriptor, IOHIDReportType type, IOOptionBits options)
{
	// The first byte of the report tells what kind of report it is.
	UInt8 opcode;
	IOByteCount read = descriptor->readBytes(0, &opcode, sizeof opcode);
	if (read != 1)
	{
		IO_LOG_DEBUG("Couldn't read a single byte from report descriptor!");
		return kIOReturnNoMemory;
	}
	
	// 0x20 is a button state report, anything else should be ignored (at least until we figure out what they are).
	if (opcode != 0x20)
	{
		return kIOReturnSuccess;
	}
	
	return super::handleReport(descriptor, type, options);
}
