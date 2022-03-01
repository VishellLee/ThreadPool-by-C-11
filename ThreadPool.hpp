#pragma once
#include <thread>
#include <condition_variable>
#include <mutex>
#include <future>
#include <deque>
#include <vector>
#include <memory>
#include <functional>
#include <type_traits>

//�̳߳�
class ThreadPool
{
public:
	using TaskType = std::function<void(void)>;

	ThreadPool(const std::size_t theadSize = std::thread::hardware_concurrency() - 1);

	~ThreadPool();

	//�̳߳��ύ��������������ֵ������ʽ���Σ��������޿������캯�����Զ���������ʽ���Σ�������Ϊ������ȷ�����̲߳�������������
	//�������� std::ref �� std::cref ��װ��ǿ�ƴ�������
	//���� ��ͨ��������������lambda���ʽ����ľ�̬��Ա����
	template <typename F, typename... Args>
	auto submit(F&& f, Args&&... args)->std::future<decltype(f(std::forward<Args&>(args)...))>;

	//�̳߳��ύ��������������ֵ������ʽ���Σ��������޿������캯�����Զ���������ʽ���Σ�������Ϊ������ȷ�����̲߳�������������
	//�������� std::ref �� std::cref ��װ��ǿ�ƴ�������
	//���� ��ķǾ�̬��Ա����
	template<typename F, typename ObjPtr, typename... Args>
	auto submit(F&& f, ObjPtr&& ptr, Args&&... args)->std::future<decltype((ptr->*f)(std::forward<Args&>(args)...))>;

	//�̳߳������ύ������,tuple�еĶ���������ֵ������ʽ���Σ��������޿������캯�����Զ���������ʽ���Σ�������Ϊ������ȷ�����̲߳�������������
	//tuple�еĶ������ std::ref �� std::cref ��װ��ǿ�ƴ�������
	//ʹ�����κοɵ��ö���(��ͨ��������������lambda���ʽ����ľ�̬��Ա��������ķǾ�̬��Ա����)
	template <typename... Args, typename ReturnType>
	void submitInBatch(std::vector<std::tuple<Args...>>& functors, std::vector<std::future<ReturnType>>& taskFutures);

	//�ر��̳߳�
	void shutDown();

	std::size_t threadSize(void) const { return m_threadSize; }

private:
	//�̳߳ص��Ⱥ���
	void scheduled();

	//�º���������װ����ʹ�� SFINAE ���п����������ֵ����ֵ����
	template <typename T>
	static T& functorParameterWrapper(T& value,
		typename std::enable_if <std::is_copy_constructible<typename std::decay<T>::type>::value>::type* = nullptr)
	{
		return value;
	}

	//�º���������װ����ʹ�� SFINAE ��û�п����������ֵ, �����Խ������ð�װ, ���װΪ���ô���
	template <typename T>
	static std::reference_wrapper<T>
		functorParameterWrapper(T& value,
			typename std::enable_if<(std::is_object<typename std::decay<T>::type>::value || std::is_function<typename std::decay<T>::type>::value)
			&& !std::is_copy_constructible<typename std::decay<T>::type>::value>::type* = nullptr)
	{
		return std::ref(value);
	}

	//�º���������װ����ʹ�� SFINAE ���п����������ֵ��������ת��
	template <typename T>
	static T&& functorParameterWrapper(T&& value,
		typename std::enable_if<std::is_rvalue_reference<decltype(value)>::value &&
		(std::is_copy_constructible<typename std::decay<T>::type>::value ||
			std::is_move_constructible<typename std::decay<T>::type>::value)>::type* = nullptr)
	{
		return std::forward<T>(value);
	}

	//�º���������װ����ʹ�� SFINAE ��û�п����������ֵ, �����Խ������ð�װ, ���װΪ���ô���
	template <typename T>
	static std::reference_wrapper<T>
		functorParameterWrapper(T&& value,
			typename std::enable_if<std::is_rvalue_reference<decltype(value)>::value && (std::is_object<typename std::decay<T>::type>::value || std::is_function<typename std::decay<T>::type>::value)
			&& !std::is_copy_constructible<typename std::decay<T>::type>::value && !std::is_move_constructible<typename std::decay<T>::type>::value>::type* = nullptr)
	{
		return std::cref(value);
	}

	//ͨ����ģ���ƫ�ػ�����tuple��������������װ
	template <typename Tuple, typename ReturnType, bool Done, int Total, int... N>
	struct wrapperTasksImpl
	{
		static std::function<void(void)> wrapper(Tuple&& t, std::future<ReturnType>& tskFuture)
		{
			return wrapperTasksImpl<Tuple, ReturnType, Total == 2 + sizeof...(N), Total, N..., sizeof...(N)>::wrapper(std::forward<Tuple>(t), tskFuture);
		}
	};

	//ͨ����ģ���ƫ�ػ�����tuple��������������װ
	template <typename Tuple, typename ReturnType, int Total, int... N>
	struct wrapperTasksImpl<Tuple, ReturnType, true, Total, N...>
	{
		static std::function<void(void)> wrapper(Tuple&& t, std::future<ReturnType>& tskFuture)
		{
			auto tskFunc = std::bind(functorParameterWrapper(std::get<0>(std::forward<Tuple>(t))), functorParameterWrapper(std::get<N + 1>(std::forward<Tuple>(t)))...);
			std::shared_ptr<std::packaged_task<decltype(tskFunc())(void)>> tskPtr = std::make_shared<std::packaged_task<decltype(tskFunc())(void)>>(std::move(tskFunc));
			tskFuture = tskPtr->get_future();
			std::function<void(void)> task = [tskPtr]() {(*tskPtr)(); };

			return task;
		}
	};

	//��tuple�ڵ� �ɵ��ö��� �� ���� ��װΪ �̳߳��������
	template <typename Tuple, typename ReturnType>
	std::function<void(void)> wrapperTasksFromTuple(Tuple&& t, std::future<ReturnType>& tskFuture)
	{
		using tuple_type = typename std::decay<Tuple>::type;
		return wrapperTasksImpl<Tuple, ReturnType, 1 == std::tuple_size<tuple_type>::value, std::tuple_size<tuple_type>::value>::wrapper(std::forward<Tuple>(t), tskFuture);
	}

	std::deque<TaskType> m_taskQue;
	std::vector<std::thread> m_theadVec;
	std::condition_variable cond_var;
	std::mutex m_mu;

	bool m_isShutDown;
	std::size_t m_threadSize;
};

inline ThreadPool::ThreadPool(const std::size_t theadSize)
	:m_isShutDown(false), m_threadSize(theadSize)
{
	if (m_threadSize < 0 || m_threadSize > 7)
	{
		m_threadSize = 3;
	}

	m_theadVec.reserve(m_threadSize);

	for (uint32_t i = 0; i < m_threadSize; i++)
	{
		m_theadVec.emplace_back(&ThreadPool::scheduled, this);
	}
}

inline ThreadPool::~ThreadPool()
{
	shutDown();
}

template<typename F, typename... Args> inline
auto ThreadPool::submit(F&& f, Args&& ...args) -> std::future<decltype(f(std::forward<Args&>(args)...))>
{
	using functor_result_type = typename std::result_of<F& (Args&...)>::type;

	std::function<functor_result_type(void)> funcTask = std::bind(functorParameterWrapper(std::forward<F>(f)), functorParameterWrapper(std::forward<Args>(args))...);
	std::shared_ptr<std::packaged_task<functor_result_type(void)>> taskPtr = std::make_shared<std::packaged_task<functor_result_type(void)>>(funcTask);
	TaskType wrapperTask = [taskPtr](void)->void { (*taskPtr)(); };

	{
		std::lock_guard<std::mutex> gurad(m_mu);
		m_taskQue.push_back(wrapperTask);
	}

	cond_var.notify_one();

	return taskPtr->get_future();
}

template<typename F, typename ObjPtr, typename... Args> inline
auto ThreadPool::submit(F&& f, ObjPtr&& ptr, Args&&... args) -> std::future<decltype((ptr->*f)(std::forward<Args&>(args)...))>
{
	using functor_result_type = decltype((ptr->*f)(std::forward<Args&>(args)...));

	std::function<functor_result_type(void)> funcTask = std::bind(functorParameterWrapper(std::forward<F>(f)), functorParameterWrapper(std::forward<ObjPtr>(ptr)), functorParameterWrapper(std::forward<Args>(args))...);
	std::shared_ptr<std::packaged_task<functor_result_type(void)>> taskPtr = std::make_shared<std::packaged_task<functor_result_type(void)>>(funcTask);
	TaskType wrapperTask = [taskPtr](void)->void { (*taskPtr)(); };

	{
		std::lock_guard<std::mutex> gurad(m_mu);
		m_taskQue.push_back(wrapperTask);
	}

	cond_var.notify_one();

	return taskPtr->get_future();
}

template<typename... Args, typename ReturnType> inline
void ThreadPool::submitInBatch(std::vector<std::tuple<Args...>>& functors, std::vector<std::future<ReturnType>>& taskFutures)
{
	std::deque<std::function<void(void)>> tasks;

	for (auto& functor : functors)
	{
		std::future<ReturnType> taskFuture;
		tasks.emplace_back(wrapperTasksFromTuple(functor, taskFuture));
		taskFutures.emplace_back(std::move(taskFuture));
	}

	{
		std::lock_guard<std::mutex> guard(m_mu);
		m_taskQue.insert(m_taskQue.end(), tasks.begin(), tasks.end());
	}

	if (tasks.size() == 1)
	{
		cond_var.notify_one();
	}
	else if (tasks.size() > 1)
	{
		cond_var.notify_all();
	}
}

inline void ThreadPool::scheduled()
{
	while (true)
	{
		std::unique_lock<std::mutex> guard(m_mu);

		cond_var.wait(guard, [this]() {return !m_taskQue.empty() || m_isShutDown; });
		if (m_isShutDown)
		{
			break;
		}

		auto task = m_taskQue.front();
		m_taskQue.pop_front();
		guard.unlock();
		task();
	}
}

inline void ThreadPool::shutDown()
{
	{
		std::lock_guard<std::mutex> gurad(m_mu);
		m_isShutDown = true;
	}

	cond_var.notify_all();

	for (std::size_t i = 0; i < m_threadSize; i++)
	{
		if (m_theadVec[i].joinable())
		{
			m_theadVec[i].join();
		}
	}
}
