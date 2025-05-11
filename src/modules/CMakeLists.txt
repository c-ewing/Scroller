target_sources(app PRIVATE
	       ${CMAKE_CURRENT_SOURCE_DIR}/scroller_usb.c
		   ${CMAKE_CURRENT_SOURCE_DIR}/scroller_scroll_calculate.c
		   ${CMAKE_CURRENT_SOURCE_DIR}/scroller_idle_waker.c
)