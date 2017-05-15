#pragma once

#include "config.h"
#include <iostream>
#include <unsupported/Eigen/CXX11/Tensor>

class CSoftmaxLayer
{
public:
	
	static void CSoftmaxLayer::softmax_function(const Tensor2xf &inputs, Tensor2xf &softmax) {
		softmax = inputs.exp();
		Tensor2xf sorfmax_rowsum = softmax.sum(Eigen::array<int, 1>{ {1}}).reshape(Eigen::array<int, 2>{ {inputs.dimension(0), 1}});//��һά��ά,��߲�����auto������ּ�����ֵ����
		auto sorfmax_rowsum_broad = sorfmax_rowsum.broadcast(Eigen::array<int, 2> { {1, inputs.dimension(1)}});
		
		softmax = softmax/ sorfmax_rowsum_broad;//�й�һ��
	}
	//����ǰһ�����羭��ȫ����input=wx+b�󣬾���soft��ʧ�����󵼣�sotfmax_net-pro_real
	//https://math.stackexchange.com/questions/945871/derivative-of-softmax-loss-function
	static void CSoftmaxLayer::softmax_loss_forward_backward(const Tensor2xf &inputs, const Tensor1xf&label, Tensor2xf &d_inputs, Tensor0xf &loss) {

		Tensor2xf softmax;
		softmax_function(inputs, softmax);

		Tensor2xf real_label(softmax.dimension(0), softmax.dimension(1));
		real_label.setZero();
		assert(label.dimension(0) == inputs.dimension(0));
		for (int i = 0; i < label.dimension(0); i++)
		{
			real_label(i, label(i)) = 1;
		}

		Tensor2xf losst = real_label*softmax.log();
		loss =-losst.mean();//��������ʧ����ƽ��ֵ��ƽ��ֵ�����󵼵�ʱ����Ҫ��ס�ݶȳ���
		d_inputs = (softmax - real_label)*(1.f/float(inputs.size()));//����loss�����ʱ������һ���Ǽ���loss mean�����Է����󵼵�ʱ����Ҫ����(inputs.rows()*inputs.cols())*/

	}
	static void CSoftmaxLayer::test() {
		int batch_size = 4;
		int input_size = 3;
		int output_size = 2;


		float input_data[12] = {1, 2, 3, 6, 4, 5, 2, 8, 10, 12, 11, 9};
		Eigen::TensorMap<Tensor2xf>inputs(input_data, batch_size, input_size);
		float label_data[4] = { 1, 0, 1, 1 };
		Eigen::TensorMap<Tensor1xf> label(label_data,batch_size);
		

		Tensor2xf d_inputs;
		Tensor0xf loss;
		CSoftmaxLayer::softmax_loss_forward_backward(inputs, label, d_inputs, loss);
		std::cout << loss << std::endl;
		std::cout << d_inputs << std::endl;
	}
};

