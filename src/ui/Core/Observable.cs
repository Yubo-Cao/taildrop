using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Runtime.CompilerServices;

namespace TailDrop.UI.Core;

public abstract partial class Observable : INotifyPropertyChanged
{
    public event PropertyChangedEventHandler? PropertyChanged;

    protected bool Set<T>(ref T field, T value, [CallerMemberName] string? name = null)
    {
        if (EqualityComparer<T>.Default.Equals(field, value)) return false;
        field = value;
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
        return true;
    }

    protected void Raise(string name) => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
}

public static class Collections
{
    /// <summary>
    /// Makes `list` match `models` in order, keeping existing view models (so
    /// list transitions animate moves instead of rebuilding everything).
    /// </summary>
    public static void Sync<TModel, TVm>(ObservableCollection<TVm> list, IReadOnlyList<TModel> models,
        Func<TModel, string> modelKey, Func<TVm, string> vmKey, Func<TModel, TVm> create, Action<TVm, TModel> update)
    {
        var keys = new HashSet<string>(models.Select(modelKey));
        for (int i = list.Count - 1; i >= 0; i--)
            if (!keys.Contains(vmKey(list[i])))
                list.RemoveAt(i);
        for (int i = 0; i < models.Count; i++)
        {
            string key = modelKey(models[i]);
            int at = -1;
            for (int j = i; j < list.Count; j++)
            {
                if (vmKey(list[j]) == key)
                {
                    at = j;
                    break;
                }
            }
            if (at < 0)
            {
                list.Insert(i, create(models[i]));
            }
            else
            {
                if (at != i) list.Move(at, i);
                update(list[i], models[i]);
            }
        }
    }
}
