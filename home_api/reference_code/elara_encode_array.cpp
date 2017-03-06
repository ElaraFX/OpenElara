
/** The example to use Base85 encoding to write array.
 */
void ei_end_tab()
{
	eiTLS			*tls;
	eiContext		*context;

	tls = ei_job_get_tls();
	context = (eiContext *)ei_tls_get_interface(tls, EI_TLS_TYPE_CONTEXT);

	eiTag tab_tag = context->current_tab;
	if (context->echo_enabled && context->echo_file != NULL && 
		tab_tag != EI_NULL_TAG && g_binary_encoding)
	{
		eiInt item_type = ei_data_table_type(tab_tag);

		if (item_type != EI_TYPE_TOKEN && 
			item_type != EI_TYPE_TAG && 
			item_type != EI_TYPE_TAG_NODE && 
			item_type != EI_TYPE_TAG_ARRAY)
		{
			eiDataTableIterator iter;
			ei_data_table_begin(tab_tag, &iter);

			eiInt src_size = iter.item_count * iter.nkeys * iter.item_size;
			eiInt dst_size = ei_base85_calc_encode_bound(src_size);
			char *dst_buf = (char *)ei_allocate(dst_size);

			ei_base85_encode((eiByte *)ei_data_table_read(&iter, 0, 0), src_size, dst_buf);

			fprintf(context->echo_file, " %s", dst_buf);

			EI_CHECK_FREE(dst_buf);

			ei_data_table_end(&iter);
		}
	}

	context->current_tab = EI_NULL_TAG;
}

/** Standard conforming implementation of Base85 encoding
 * reference:
 * https://raw.githubusercontent.com/zeromq/rfc/master/src/spec_32.c
 */
// Maps base 256 to base 85
static const char encoder[85 + 1] = {
    "0123456789" 
    "abcdefghij" 
    "klmnopqrst" 
    "uvwxyzABCD"
    "EFGHIJKLMN" 
    "OPQRSTUVWX" 
    "YZ.-:+=^!/" 
    "*?&<>()[]{" 
    "}@%$#"
};

eiUint32 ei_base85_calc_encode_bound(eiUint32 input_length)
{
	eiUint32 padding_size = 0;
	eiUint32 remainder = input_length % 4;
	if (remainder > 0)
	{
		padding_size = 4 - remainder;
	}
	eiUint32 padded_length = input_length + padding_size;
	eiUint32 output_length = (padded_length / 4) * 5 - padding_size;
	return output_length + 1;
}

eiUint32 ei_base85_encode(const eiByte *data, eiUint32 input_length, char *encoded_data)
{
	eiUint32 remainder = input_length % 4;
	eiUint32 padding_size = 0;
	if (remainder > 0)
	{
		padding_size = 4 - remainder;
	}
	eiUint32 padded_length = input_length + padding_size;
	eiUint32 output_length = (padded_length / 4) * 5 - padding_size;

	eiUint32 char_nbr = 0;
	for (eiUint32 byte_nbr = 0; byte_nbr < padded_length; byte_nbr += 4)
	{
		eiUint32 value = 0;
		if (byte_nbr + 0 < input_length)
		{
			value += (data[byte_nbr + 0] << 3 * 8);
		}
		if (byte_nbr + 1 < input_length)
		{
			value += (data[byte_nbr + 1] << 2 * 8);
		}
		if (byte_nbr + 2 < input_length)
		{
			value += (data[byte_nbr + 2] << 1 * 8);
		}
		if (byte_nbr + 3 < input_length)
		{
			value += data[byte_nbr + 3];
		}
		if (char_nbr < output_length)
		{
			encoded_data[char_nbr] = encoder[value / (85 * 85 * 85 * 85) % 85];
			++ char_nbr;
		}
		if (char_nbr < output_length)
		{
			encoded_data[char_nbr] = encoder[value / (85 * 85 * 85) % 85];
			++ char_nbr;
		}
		if (char_nbr < output_length)
		{
			encoded_data[char_nbr] = encoder[value / (85 * 85) % 85];
			++ char_nbr;
		}
		if (char_nbr < output_length)
		{
			encoded_data[char_nbr] = encoder[value / 85 % 85];
			++ char_nbr;
		}
		if (char_nbr < output_length)
		{
			encoded_data[char_nbr] = encoder[value % 85];
			++ char_nbr;
		}
	}

	encoded_data[char_nbr] = '\0';

    return char_nbr + 1;
}
