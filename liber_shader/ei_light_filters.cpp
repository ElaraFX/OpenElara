/**************************************************************************
 * Copyright (C) 2013 Elvic Liang<len3dev@gmail.com>
 * All rights reserved.
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *************************************************************************/

#include "photometric.h"

/** Light filter based on distance decay and photometric web
 */
light (std_light_filter)

	enum
	{
		e_use_near_atten = 0, 
		e_near_start, 
		e_near_stop, 
		e_use_far_atten, 
		e_far_start, 
		e_far_stop, 
		e_use_web_dist, 
		e_web_filename, 
		e_web_scale, 
		e_web_normalize, 
	};

	static void parameters()
	{
		declare_bool(use_near_atten, EI_FALSE);
		declare_scalar(near_start, 10.0f);
		declare_scalar(near_stop, 100.0f);
		declare_bool(use_far_atten, EI_FALSE);
		declare_scalar(far_start, 500.0f);
		declare_scalar(far_stop, 1000.0f);
		declare_bool(use_web_dist, EI_FALSE);
		declare_token(web_filename, NULL);
		declare_scalar(web_scale, 1.0f);
		declare_bool(web_normalize, EI_FALSE);
	}

	static void init()
	{
	}

	static void exit()
	{
	}

	void init_node()
	{
		glob = NULL;

		if (eval_bool(use_web_dist))
		{
			eiToken web_filename = eval_token(web_filename);
			eiBool web_normalize = eval_bool(web_normalize);
			eiScalar web_scale = eval_scalar(web_scale);

			if (web_filename.str != NULL)
			{
				PhotometricSampler *sampler = new PhotometricSampler;
				if (sampler->load(web_filename.str, true, web_normalize))
				{
					sampler->m_max_intensity *= web_scale;
					glob = sampler;
				}
				else
				{
					delete sampler;
				}
			}
		}
	}

	void exit_node()
	{
		if (glob != NULL)
		{
			PhotometricSampler *sampler = (PhotometricSampler *)glob;
			delete sampler;
			glob = NULL;
		}
	}

	void main(void *arg)
	{
		eiScalar atten = 1.0f;

		const eiBool use_near_atten = eval_bool(use_near_atten);
		const eiBool use_far_atten = eval_bool(use_far_atten);

		if (use_near_atten || use_far_atten)
		{
			const eiScalar d = len(Ps);

			if (use_near_atten)
			{
				const eiScalar near_start = eval_scalar(near_start);
				const eiScalar near_stop = eval_scalar(near_stop);

				atten *= smoothstep(near_start, near_stop, d);
			}

			if (use_far_atten)
			{
				const eiScalar far_start = eval_scalar(far_start);
				const eiScalar far_stop = eval_scalar(far_stop);

				atten *= (1.0f - smoothstep(far_start, far_stop, d));
			}
		}

		PhotometricSampler *sampler = (PhotometricSampler *)glob;

		if (sampler != NULL)
		{
			const eiVector dir = normalize(I);
			const eiScalar web_scale = eval_scalar(web_scale);

			atten *= (web_scale * sampler->sample(dir));
		}

		eiColor *light_color = (eiColor *)arg;
		(*light_color) *= atten;
	}

end_shader (std_light_filter)
