#ifndef HID_EXTENSIONS_H
#define HID_EXTENSIONS_H

/**
 * @brief Define HID Physical Minimum item with the data length of one byte.
 *
 * @param a Minimum value in logical units
 * @return  HID Physical Minimum item
 */
#define HID_PHYSICAL_MIN8(a) HID_ITEM(HID_ITEM_TAG_PHYSICAL_MIN, HID_ITEM_TYPE_GLOBAL, 1), a

/**
 * @brief Define HID Physical Maximum item with the data length of one byte.
 *
 * @param a Maximum value in logical units
 * @return  HID Physical Maximum item
 */
#define HID_PHYSICAL_MAX8(a) HID_ITEM(HID_ITEM_TAG_PHYSICAL_MAX, HID_ITEM_TYPE_GLOBAL, 1), a

/**
 * @brief Define HID Physical Minimum item with the data length of two bytes.
 *
 * @param a Minimum value lower byte
 * @param b Minimum value higher byte
 * @return  HID Physical Minimum item
 */
#define HID_PHYSICAL_MIN16(a, b) HID_ITEM(HID_ITEM_TAG_PHYSICAL_MIN, HID_ITEM_TYPE_GLOBAL, 2), a, b

/**
 * @brief Define HID Physical Maximum item with the data length of two bytes.
 *
 * @param a Minimum value lower byte
 * @param b Minimum value higher byte
 * @return  HID Physical Maximum item
 */
#define HID_PHYSICAL_MAX16(a, b) HID_ITEM(HID_ITEM_TAG_PHYSICAL_MAX, HID_ITEM_TYPE_GLOBAL, 2), a, b

#define HID_USAGE_16(a, b) HID_ITEM(HID_ITEM_TAG_USAGE, HID_ITEM_TYPE_LOCAL, 2), a, b

#endif // HID_EXTENSIONS_H