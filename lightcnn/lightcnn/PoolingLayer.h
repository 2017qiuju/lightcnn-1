#pragma once
#include "config.h"
#include <vector>
enum PoolingMethod
{
	max,
	avg

};



class CPoolingLayer
{
public:

	CPoolingLayer(std::vector<int>pooling_shape,PaddingMethod padding_method,PoolingMethod pooling_method) {
		m_hksize = pooling_shape[0];
		m_wksize = pooling_shape[1];
		m_hstride = pooling_shape[2];
		m_wstride = pooling_shape[3];

		m_padding_method = padding_method;
		m_pooling_method = pooling_method;



		
	};
	~CPoolingLayer() {
	};
	//返回(bottom[0],bottom[1]/hstride*bottom[2]/wstride,hsize,wsize,bottom[3])
	void CPoolingLayer::extract_image_patches(const Tensor4xf &bottom, Tensor5xf &patches) {
		//这个Eigen tensor类的extract_image_patches函数，由于有数据存储列排列、行排列两种不同的模式。
		//在下面函数中，如果是采用rowmajor，下面的调用方式才是正确的
		//不能采用bottom.extract_image_patches(  m_hksize,m_wksize, m_hstride,m_wstride, 1, 1);
		switch (m_padding_method)
		{
		case valid:
			patches = bottom.extract_image_patches(m_hksize, m_wksize, m_hstride, m_wstride, 1, 1,
				Eigen::PADDING_VALID);
			break;
		case same:
			patches = bottom.extract_image_patches(m_hksize, m_wksize, m_hstride, m_wstride, 1, 1,
				Eigen::PADDING_SAME );
			break;
		default:
			break;
		}
			
	}
	Eigen::DSizes<int, 4> CPoolingLayer::get_top_shape(const Tensor4xf&bottom) {
		Eigen::DSizes<int, 4>top_shape;
		top_shape[0] = bottom.dimension(0);
		switch (m_padding_method)
		{
		case valid:
			top_shape[1] = Eigen::divup(float(bottom.dimension(1) - m_hksize + 1), float(m_hstride));
			top_shape[2] = Eigen::divup(float(bottom.dimension(2) - m_wksize + 1), float(m_wstride));
			break;
		case same:
			top_shape[1] = Eigen::divup(float(bottom.dimension(1)), float(m_hstride));
			top_shape[2] = Eigen::divup(float(bottom.dimension(2)), float(m_wstride));

			break;
		default:
			break;
		}
		top_shape[3] = bottom.dimension(3);
		return top_shape;
	}


	void CPoolingLayer::forward(const Tensor4xf&bottom,Tensor4xf&top, const Eigen::ThreadPoolDevice &device) {
		Eigen::array<int, 2> reduction_dims{1,2};//第二维、第三维的大小等于（hksize、wksize）
		Eigen::DSizes<int, 4>post_reduce_dims=get_top_shape(bottom);
		Tensor5xf patches; //(bottom[0], bottom[1] / hstride*bottom[2] / wstride, hsize, wsize, bottom[3])
		extract_image_patches(bottom, patches);
		//CBaseFunction::print_shape(patches);


		Tensor3xf pooling(post_reduce_dims[0],post_reduce_dims[1]*post_reduce_dims[2],post_reduce_dims[3]);
		switch (m_pooling_method)
		{
		case avg:
			pooling.device(device) = patches.mean(reduction_dims);//对reduction_dims内对应的维度索引进行统计，比如统计第3、2
			break;
		case max:
			pooling.device(device) = patches.maximum(reduction_dims);//最大池化
			break;
		default:
			break;
		}		
		top=pooling.reshape(post_reduce_dims);

	}
	//本函数主要用于索引解码，从一维索引到获取多维下标值。主要原因在于：max
	std::vector<int> CPoolingLayer::decode_index(std::vector<int>dim,int index) {
		std::vector<int>result;
		for (int i=0;i<5;i++)
		{
			int accu = 1;
			for (int j=5-1;j>i;j--)
			{
				accu *= dim[j];

			}
			result.push_back(std::floor(index / accu));
			index = index%accu;
		}

		return result;

	}
	//主要是重叠池化的时候，反向求导的时候是微分值累加。
	void CPoolingLayer::maxpooling_backward(const Tensor5xf &top_patches,const Eigen::DSizes<int, 4>&post_reduce_dims,
		const Tensor4xf&dtop,Tensor4xf&dbottom) {

		Eigen::array<Eigen::DenseIndex, 2> reduce_dims{ 1,2};
		auto index_tuples = top_patches.index_tuples();
		Eigen::Tensor<Eigen::Tuple<Eigen::DenseIndex, float>, 3, Eigen::internal::traits<Tensor5xf>::Layout> reduced_by_dims;
		reduced_by_dims = index_tuples.reduce(reduce_dims, Eigen::internal::ArgMaxTupleReducer<Eigen::Tuple<Eigen::DenseIndex, float> >());



		Tensor3xf reshape = dtop.reshape(Eigen::array<int, 3>{post_reduce_dims[0], post_reduce_dims[1] * post_reduce_dims[2], post_reduce_dims[3]});
		//求dpatches

/*
		for (int i = 0; i<reduced_by_dims.size(); i++)//最大池化反向求导
		{
			const Eigen::Tuple<Eigen::DenseIndex, float>& v = reduced_by_dims(i);
			dpatches.data()[v.first] = reshape.data()[i];
		}*/


		
		for (int i = 0; i < reduced_by_dims.size(); i++)//最大池化反向求导
		{
		const Eigen::Tuple<Eigen::DenseIndex, float>& v = reduced_by_dims(i);
		std::vector<int>patches_index=decode_index({ top_patches.dimension(0),
			top_patches.dimension(1),top_patches.dimension(2)
		,top_patches.dimension(3) ,top_patches.dimension(4)},v.first);


		int height = (patches_index[3] / post_reduce_dims[1])*m_hstride+patches_index[1];
		int width = (patches_index[3]% post_reduce_dims[1])*m_wstride+patches_index[2];
		if (height>=dbottom.dimension(1) ||width>=dbottom.dimension(2))
		{
			std::cout<<"out of range:"<<height<<" : "<<width<<std::endl;
			continue;
		}
		auto &element_dbottom = dbottom(patches_index[0], height,width, patches_index[4]);
		element_dbottom += reshape.data()[i];
		}
	}
	//均值池化，也可以看成是卷积
	void CPoolingLayer::avgpooling_backward(const Tensor4xf&dtop, Tensor4xf&dbottom) {
/*
		//与forward阶段相反，需要reshape回去
		Tensor5xf dpatches(top_patches.dimensions());
		dpatches.setZero();

		Tensor3xf reshape = dtop.reshape(Eigen::array<int, 3>{post_reduce_dims[0], post_reduce_dims[1] * post_reduce_dims[2], post_reduce_dims[3]});*/
		
		Tensor4xf mean_coffe = dtop*(1.f / (m_wksize*m_hksize));//均值池化反向求导要先除以均值系数

		for (int i=0;i<mean_coffe.dimension(0);i++)
		{
			for (int j=0;j<mean_coffe.dimension(1);j++)
			{
				for (int k=0;k<mean_coffe.dimension(2);k++)
				{
					for (int h=0;h<mean_coffe.dimension(3);h++)
					{
						const auto &mean_element= mean_coffe(i, j, k, h);
						for (int hh=0;hh<m_hksize;hh++)
						{
							for (int ww=0;ww<m_wksize;ww++)
							{
								int patch_height = j*m_hstride + hh;
								int patch_width = k*m_wstride + ww;
								if (patch_height<dbottom.dimension(1) &&patch_width<dbottom.dimension(2))
								{
									dbottom(i, patch_height, patch_width, h) += mean_element;
								}
								
							}
						}
					}
				}
			}
		}

/*

		//求dbottom
		for (int i = 0; i < top_patches.dimension(0); i++)
		{
			for (int j = 0; j < top_patches.dimension(1); j++)
			{
				int height = j / post_reduce_dims[1];
				int width = j% post_reduce_dims[1];
				for (int k = 0; k < top_patches.dimension(4); k++)
				{

					//把每个dpathches的pathces数值加到dbottom中
					for (int hp = 0; hp < top_patches.dimension(2); hp++)
					{
						for (int wp = 0; wp < top_patches.dimension(3); wp++)
						{
							auto &element_dbottom = dbottom(i, m_hstride*height + hp, m_wstride*width + wp, k);
							element_dbottom += top_patches(i, j, hp, wp, k);
						}
					}

				}
			}

		}*/

	}
	void CPoolingLayer::backward(const Tensor4xf&bottom,const Tensor4xf&dtop, Tensor4xf&dbottom, const Eigen::ThreadPoolDevice &device) {
		dbottom.setZero();
		Tensor5xf patches; //(bottom[0], bottom[1] / hstride*bottom[2] / wstride, hsize, wsize, bottom[3])
		extract_image_patches(bottom, patches);
		Eigen::DSizes<int, 4>post_reduce_dims = get_top_shape(bottom);
		//计算第2、3维的降维
		switch (m_pooling_method)
		{
		case max:
			maxpooling_backward(patches, post_reduce_dims, dtop, dbottom);
			break;
		case avg:
			avgpooling_backward(dtop, dbottom);
			break;
		default:
			break;
		}




	}
private:
	int m_hksize;//池化块的长宽
	int m_wksize;
	int m_hstride;//池化步长
	int m_wstride;
	
	PaddingMethod m_padding_method;//边界处理方法
	PoolingMethod m_pooling_method;//池化方法：均值池化、最大池化等

};


class CPoolingLayer_test
{
public:
	static void CPoolingLayer_test::test() {


		Eigen::ThreadPool *tp = new Eigen::ThreadPool(8);
		Eigen::ThreadPoolDevice device(tp, 8);

		int batch_size = 1;
		int input_channel = 1;
		int input_height = 7;
		int input_width = 7;
		int kenel_height = 2;
		int kenel_widht = 2;
		int khstride =3;
		int kwstride = 3;


		Tensor4xf bottom(batch_size, input_height, input_width, input_channel);
		int count = 0;
		for (int i=0;i<batch_size;i++)
		{
			for (int j=0;j<input_height;j++)
			{
				for (int k=0;k<input_width;k++)
				{
					for (int h=0;h<input_channel;h++)
					{
						bottom(i, j, k, h) = 0.1f*count;
						count++;
					}
				}
			}
		}


		Tensor1xf label_1d(batch_size);
		for (int i = 0; i < batch_size; i++)
		{
			label_1d(i) = i;
		}




		
		


		//第一层：pooling层
		CPoolingLayer layer({kenel_height,kenel_widht,khstride,kwstride },PaddingMethod::same,PoolingMethod::avg
		);


		Tensor4xf top;
		layer.forward(bottom, top,device);

		Tensor2xf top_flatten;
		CBaseFunction::flatten(top, top_flatten);


		//第二层：sotfmax网络层
		Tensor2xf one_hot;
		CBaseFunction::onehot(label_1d, top_flatten.dimension(1), one_hot);
		Tensor2xf dtop_flatten(top_flatten);
		float loss = CBaseFunction::softmax_with_loss(top_flatten, one_hot, dtop_flatten, device);

		Tensor4xf dtop;
		CBaseFunction::reshape_like(dtop_flatten, top, dtop);



		Tensor4xf dbottom(bottom);

		layer.backward(bottom, dtop,dbottom,device);


		Tensor4rf dbottom_swap = dbottom.swap_layout();
		std::cout << "***************forward************" << std::endl;
		//CBaseFunction::print_shape(one_hot);
		CBaseFunction::print_shape(top);
		CBaseFunction::print_element(top);
		//std::cout << "bottom" << bottom<< std::endl;
		//std::cout << "top" << top << std::endl;
		//std::cout << "dbottom" << dbottom << std::endl;
		std::cout << "loss" << loss << std::endl;
		
		//std::cout << "dbottom" << dbottom << std::endl;
		//std::cout << "dtop" << top << std::endl;





	}

};